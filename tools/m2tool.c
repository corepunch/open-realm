#include "tools/viewer_common.h"
#include "client/tr_public.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    int32_t count;
    int32_t offset;
} m2Array_t;

typedef struct {
    DWORD magic;
    DWORD version;
    m2Array_t name;
    DWORD flags;
    m2Array_t global_sequences;
    m2Array_t animations;
    m2Array_t animation_lookup;
    m2Array_t playable_animation_lookup;
    m2Array_t bones;
    m2Array_t key_bone_lookup;
    m2Array_t vertices;
    m2Array_t views;
    m2Array_t colors;
    m2Array_t textures;
    m2Array_t transparency_lookup;
    m2Array_t texture_flipbooks;
    m2Array_t texture_animations;
    m2Array_t color_replacements;
    m2Array_t render_flags;
    m2Array_t bone_lookup_table;
    m2Array_t texture_lookup_table;
    m2Array_t texture_units;
    m2Array_t transparency_lookup_table;
    m2Array_t texture_animation_lookup_table;
    VECTOR3 bounding_box_min;
    VECTOR3 bounding_box_max;
    float bounding_sphere_radius;
    VECTOR3 collision_box_min;
    VECTOR3 collision_box_max;
    float collision_sphere_radius;
    m2Array_t bounding_triangles;
    m2Array_t bounding_vertices;
    m2Array_t bounding_normals;
    m2Array_t attachments;
    m2Array_t attachment_lookup;
    m2Array_t events;
    m2Array_t lights;
    m2Array_t cameras;
    m2Array_t camera_lookup;
    m2Array_t ribbon_emitters;
    m2Array_t particle_emitters;
} m2HeaderInfo_t;

typedef struct {
    WORD animation_id;
    WORD sub_animation_id;
    DWORD start_timestamp;
    DWORD end_timestamp;
    FLOAT movement_speed;
    DWORD flags;
    SHORT probability;
    WORD padding;
    DWORD minimum_repetitions;
    DWORD maximum_repetitions;
    DWORD blend_time;
    VECTOR3 min;
    VECTOR3 max;
    FLOAT radius;
    SHORT next_animation;
    WORD alias_next;
} m2SequenceClassic_t;

typedef struct {
    WORD animation_id;
    WORD sub_animation_id;
    DWORD length;
    FLOAT movement_speed;
    DWORD flags;
    SHORT probability;
    WORD padding;
    DWORD minimum_repetitions;
    DWORD maximum_repetitions;
    DWORD blend_time;
    VECTOR3 min;
    VECTOR3 max;
    FLOAT radius;
    SHORT next_animation;
    WORD alias_next;
} m2SequenceModern_t;

typedef struct {
    uint16_t track_type;
    uint16_t loop_index;
    m2Array_t sequence_times;
    m2Array_t sequence_keys;
} m2Track_t;

typedef struct {
    m2Array_t times;
} m2SequenceTimes_t;

typedef struct {
    m2Array_t keys;
} m2SequenceKeys_t;

typedef struct {
    uint16_t track_type;
    uint16_t loop_index;
    m2Array_t ranges;
    m2Array_t times;
    m2Array_t keys;
} m2TrackClassic_t;

typedef struct {
    DWORD start;
    DWORD end;
} m2Range_t;

typedef struct {
    DWORD auCompQ[2];
} m2CompQuat_t;

typedef struct {
    DWORD attachment_id;
    WORD bone_index;
    WORD padding;
    VECTOR3 position;
    m2Track_t visibility_track;
} m2AttachmentModern_t;

typedef struct {
    DWORD attachment_id;
    WORD bone_index;
    WORD padding;
    VECTOR3 position;
    m2TrackClassic_t visibility_track;
} m2AttachmentClassic_t;

typedef struct {
    DWORD bone_id;
    DWORD flags;
    WORD parent_index;
    WORD dist_to_parent;
    DWORD union_data;
    m2Track_t translation_track;
    m2Track_t rotation_track;
    m2Track_t scale_track;
    VECTOR3 pivot;
} m2CompBoneModern_t;

typedef struct {
    DWORD bone_id;
    DWORD flags;
    WORD parent_index;
    WORD submesh_id;
    m2TrackClassic_t translation_track;
    m2TrackClassic_t rotation_track;
    m2TrackClassic_t scale_track;
    VECTOR3 pivot;
} m2CompBoneClassic_t;

typedef struct {
    VECTOR3 pos;
    BYTE bone_weights[4];
    BYTE bone_indices[4];
    VECTOR3 normal;
    VECTOR2 tex_coords[2];
} m2Vertex_t;

typedef struct {
    DWORD type;
    DWORD flags;
    m2Array_t filename;
} m2Texture_t;

typedef struct {
    DWORD magic;
    m2Array_t vertices;
    m2Array_t indices;
    m2Array_t bones;
    m2Array_t sections;
    m2Array_t batches;
    DWORD bone_count_max;
} m2SkinHeader_t;

typedef struct {
    m2Array_t vertices;
    m2Array_t indices;
    m2Array_t bones;
    m2Array_t sections;
    m2Array_t batches;
    DWORD bone_count_max;
} m2EmbeddedView_t;

typedef struct {
    WORD skin_section_id;
    WORD level;
    WORD vertex_start;
    WORD vertex_count;
    WORD index_start;
    WORD index_count;
    WORD bone_count;
    WORD bone_combo_index;
    WORD bone_influences;
    WORD center_bone_index;
    VECTOR3 center_position;
    VECTOR3 sort_center_position;
    float sort_radius;
} m2SkinSection_t;

typedef struct {
    WORD skin_section_id;
    WORD level;
    WORD vertex_start;
    WORD vertex_count;
    WORD index_start;
    WORD index_count;
    WORD bone_count;
    WORD bone_combo_index;
    WORD bone_influences;
    WORD center_bone_index;
    VECTOR3 center_position;
} m2SkinSectionLegacy_t;

typedef struct {
    BYTE flags;
    signed char priority_plane;
    WORD shader_id;
    WORD skin_section_index;
    WORD geoset_index;
    SHORT color_index;
    WORD material_index;
    WORD material_layer;
    WORD texture_count;
    WORD texture_combo_index;
    WORD texture_coord_combo_index;
    WORD texture_weight_combo_index;
    WORD texture_transform_combo_index;
} m2Batch_t;

static HANDLE archives[64] = { 0 };
static LPCSTR g_model_path = NULL;
static LPCSTR g_skin_path = NULL;
static LPCSTR g_anim_check = NULL;
static bool g_info_only = false;
static bool g_dump_all = false;
static bool g_wow_player_config = false;
static bool g_wow_player_config_only = false;
static bool g_run_once = false;
static DWORD g_wow_appearance = (1u << 23); /* Wow_PackAppearance(..., WOW_CLASS_WARRIOR, ...) */
static DWORD g_wow_equipment = 0;
static viewer_orbit_t g_orbit;
static float g_preview_scale = 1.0f;

static void usage(void) {
    fprintf(stderr,
            "Usage:\n"
            "  m2tool -mpq <archive.mpq> -model <file.m2|file.mdx> [--viewer] [--once] [--skin <file00.skin>] [--info] [--dump-all] [--anim <name|index>] [--wow-player-config] [--wow-player-config-only] [--appearance <bits>] [--equipment <bits>]\n"
            "\n"
            "Examples:\n"
            "  m2tool -mpq data/world-of-warcraft/model.MPQ -model \"Character\\\\Orc\\\\Male\\\\OrcMale.m2\"\n"
            "  m2tool -mpq data/world-of-warcraft/model.MPQ -model \"Character\\\\Orc\\\\Male\\\\OrcMale.m2\" --info\n"
            "  m2tool -mpq data/world-of-warcraft/model.MPQ -mpq data/world-of-warcraft/dbc.MPQ -mpq data/world-of-warcraft/texture.MPQ -model \"Character\\\\Orc\\\\Male\\\\OrcMale.m2\" --wow-player-config-only\n"
            "\n"
            "Notes:\n"
            "  Viewer mode is the default, matching mdxtool.\n"
            "  --info prints header arrays, bounds, vertex bounds, textures, and skin counts, then exits.\n"
            "  --dump-all additionally prints animation rows and texture rows.\n"
            "  --anim prints per-sequence bone track diagnostics for a named or numeric animation.\n"
            "  --wow-player-config-only prints only the in-game WoW character outfit/geoset configuration.\n");
}

static void errorf(LPCSTR fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static void Tool_DrawString(refExport_t const *re, LPCSTR string, int x, int y) {
    if (!re || !string) {
        return;
    }
    for (DWORD i = 0; string[i]; i++) {
        re->DrawChar(x + (int)i * 8, y, (BYTE)string[i]);
    }
}

static BOOL PathHasExtension(LPCSTR path, LPCSTR extension) {
    size_t path_len;
    size_t ext_len;

    if (!path || !extension) {
        return false;
    }
    path_len = strlen(path);
    ext_len = strlen(extension);
    return path_len >= ext_len && strcasecmp(path + path_len - ext_len, extension) == 0;
}

static BOOL CopyWithExtension(LPCSTR path, LPCSTR extension, LPSTR out, DWORD out_size) {
    LPCSTR dot;
    size_t stem_len;

    if (!path || !extension || !out || out_size == 0) {
        return false;
    }
    dot = strrchr(path, '.');
    stem_len = dot ? (size_t)(dot - path) : strlen(path);
    if (stem_len + strlen(extension) + 1 > out_size) {
        return false;
    }
    memcpy(out, path, stem_len);
    snprintf(out + stem_len, out_size - stem_len, "%s", extension);
    return true;
}

static HANDLE OpenFileFallback(LPCSTR path, LPSTR resolved, DWORD resolved_size) {
    HANDLE file;
    PATHSTR fallback;

    if (!path || !*path) {
        return NULL;
    }
    file = Tool_OpenFile(archives, sizeof(archives) / sizeof(archives[0]), path);
    if (file) {
        snprintf(resolved, resolved_size, "%s", path);
        return file;
    }
    if (PathHasExtension(path, ".mdx") && CopyWithExtension(path, ".m2", fallback, sizeof(fallback))) {
        file = Tool_OpenFile(archives, sizeof(archives) / sizeof(archives[0]), fallback);
        if (file) {
            snprintf(resolved, resolved_size, "%s", fallback);
            return file;
        }
    }
    if (PathHasExtension(path, ".m2") && CopyWithExtension(path, ".mdx", fallback, sizeof(fallback))) {
        file = Tool_OpenFile(archives, sizeof(archives) / sizeof(archives[0]), fallback);
        if (file) {
            snprintf(resolved, resolved_size, "%s", fallback);
            return file;
        }
    }
    return NULL;
}

static LPBYTE ReadWholeFile(LPCSTR path, LPDWORD out_size, LPSTR resolved, DWORD resolved_size) {
    HANDLE file = OpenFileFallback(path, resolved, resolved_size);
    DWORD size;
    DWORD read_size = 0;
    LPBYTE data;

    if (!file) {
        return NULL;
    }
    size = SFileGetFileSize(file, NULL);
    data = Tool_MemAlloc(size ? size : 1);
    SFileSetFilePointer(file, 0, NULL, FILE_BEGIN);
    if (!SFileReadFile(file, data, size, &read_size, NULL) || read_size != size) {
        Tool_CloseFile(file);
        Tool_MemFree(data);
        return NULL;
    }
    Tool_CloseFile(file);
    *out_size = size;
    return data;
}

enum {
    M2TOOL_CHAR_TEX_UPPER_ARM,
    M2TOOL_CHAR_TEX_LOWER_ARM,
    M2TOOL_CHAR_TEX_HAND,
    M2TOOL_CHAR_TEX_UPPER_TORSO,
    M2TOOL_CHAR_TEX_LOWER_TORSO,
    M2TOOL_CHAR_TEX_UPPER_LEG,
    M2TOOL_CHAR_TEX_LOWER_LEG,
    M2TOOL_CHAR_TEX_FOOT,
    M2TOOL_CHAR_TEX_COUNT
};

enum {
    M2TOOL_SLOT_HEAD,
    M2TOOL_SLOT_SHOULDERS,
    M2TOOL_SLOT_CHEST,
    M2TOOL_SLOT_SHIRT,
    M2TOOL_SLOT_BELT,
    M2TOOL_SLOT_LEGS,
    M2TOOL_SLOT_BOOTS,
    M2TOOL_SLOT_GLOVES,
    M2TOOL_SLOT_TABARD,
    M2TOOL_SLOT_CAPE,
    M2TOOL_SLOT_COUNT
};

#define M2TOOL_NUM_GEOSET_GROUPS 16

typedef struct {
    LPBYTE data;
    DWORD size;
    DWORD records;
    DWORD fields;
    DWORD record_size;
    DWORD string_size;
    BYTE const *records_base;
    BYTE const *strings_base;
} m2ToolDbc_t;

typedef struct {
    LPCSTR texture[M2TOOL_CHAR_TEX_COUNT];
    DWORD geoset[M2TOOL_NUM_GEOSET_GROUPS];
    DWORD flags;
    DWORD display_ids[16];
    DWORD display_count;
} m2ToolWowOutfit_t;

static DWORD Read32LE(BYTE const *p) {
    return ((DWORD)p[0]) | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static BOOL LoadDbc(LPCSTR filename, m2ToolDbc_t *dbc) {
    PATHSTR resolved = { 0 };
    DWORD size = 0;

    if (!filename || !dbc) {
        return false;
    }
    memset(dbc, 0, sizeof(*dbc));
    dbc->data = ReadWholeFile(filename, &size, resolved, sizeof(resolved));
    if (!dbc->data || size <= 20 || memcmp(dbc->data, "WDBC", 4)) {
        SAFE_DELETE(dbc->data, Tool_MemFree);
        return false;
    }
    dbc->size = size;
    dbc->records = Read32LE(dbc->data + 4);
    dbc->fields = Read32LE(dbc->data + 8);
    dbc->record_size = Read32LE(dbc->data + 12);
    dbc->string_size = Read32LE(dbc->data + 16);
    if (!dbc->fields || dbc->record_size < sizeof(DWORD) ||
        20 + dbc->records * dbc->record_size + dbc->string_size > dbc->size) {
        SAFE_DELETE(dbc->data, Tool_MemFree);
        return false;
    }
    dbc->records_base = dbc->data + 20;
    dbc->strings_base = dbc->records_base + dbc->records * dbc->record_size;
    return true;
}

static void FreeDbc(m2ToolDbc_t *dbc) {
    if (!dbc) {
        return;
    }
    SAFE_DELETE(dbc->data, Tool_MemFree);
    memset(dbc, 0, sizeof(*dbc));
}

static DWORD DbcField(m2ToolDbc_t const *dbc, BYTE const *record, DWORD field) {
    if (!dbc || !record || field >= dbc->fields || field * sizeof(DWORD) + sizeof(DWORD) > dbc->record_size) {
        return 0;
    }
    return Read32LE(record + field * sizeof(DWORD));
}

static LPCSTR DbcString(m2ToolDbc_t const *dbc, DWORD offset) {
    if (!dbc || !dbc->data || offset == 0 || offset >= dbc->string_size) {
        return NULL;
    }
    return (LPCSTR)(dbc->strings_base + offset);
}

static BYTE const *DbcFindID(m2ToolDbc_t const *dbc, DWORD wanted_id) {
    if (!dbc || !dbc->data) {
        return NULL;
    }
    FOR_LOOP(i, dbc->records) {
        BYTE const *record = dbc->records_base + i * dbc->record_size;
        if (DbcField(dbc, record, 0) == wanted_id) {
            return record;
        }
    }
    return NULL;
}

static BOOL CharacterRaceGender(LPCSTR model_path, DWORD *race_id, DWORD *gender_id) {
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

static BYTE WowClassFromAppearance(DWORD appearance) {
    BYTE class_id = (BYTE)((appearance >> 23) & 0x0f);
    return class_id ? class_id : 1;
}

static DWORD ItemDisplayInfoTextureBase(m2ToolDbc_t const *dbc) {
    if (!dbc) {
        return 0;
    }
    if (dbc->fields >= 25) {
        return 15;
    }
    return dbc->fields >= 22 ? 14 : 0;
}

static DWORD ItemDisplayInfoGeosetBase(m2ToolDbc_t const *dbc) {
    return dbc && dbc->fields >= 25 ? 7 : 6;
}

static DWORD ItemDisplayInfoFlagsField(m2ToolDbc_t const *dbc) {
    return dbc && dbc->fields >= 25 ? 10 : 9;
}

static void AddDisplayInfoToOutfit(m2ToolWowOutfit_t *outfit,
                                   m2ToolDbc_t const *item_display_info,
                                   DWORD display_id,
                                   DWORD slot) {
    static DWORD const slot_geoset_group_map[M2TOOL_SLOT_COUNT][3] = {
        /* HEAD */      { 0, 0, 0 },
        /* SHOULDERS */ { 0, 0, 0 },
        /* CHEST */     { 8, 0, 12 },
        /* SHIRT */     { 0, 0, 0 },
        /* BELT */      { 0, 0, 0 },
        /* LEGS */      { 0, 9, 13 },
        /* BOOTS */     { 5, 0, 0 },
        /* GLOVES */    { 4, 0, 0 },
        /* TABARD */    { 0, 0, 0 },
        /* CAPE */      { 15, 0, 0 },
    };
    BYTE const *record;
    DWORD texture_base;
    DWORD geoset_base;
    DWORD flags_field;

    if (!outfit || !item_display_info || display_id == 0 || display_id == 0xffffffffu) {
        return;
    }
    record = DbcFindID(item_display_info, display_id);
    if (!record) {
        return;
    }
    if (outfit->display_count < sizeof(outfit->display_ids) / sizeof(outfit->display_ids[0])) {
        outfit->display_ids[outfit->display_count++] = display_id;
    }

    texture_base = ItemDisplayInfoTextureBase(item_display_info);
    geoset_base = ItemDisplayInfoGeosetBase(item_display_info);
    flags_field = ItemDisplayInfoFlagsField(item_display_info);
    FOR_LOOP(i, 3) {
        DWORD geoset_group = DbcField(item_display_info, record, geoset_base + i);
        if (geoset_group && slot < M2TOOL_SLOT_COUNT) {
            DWORD group = slot_geoset_group_map[slot][i];
            if (group && group < M2TOOL_NUM_GEOSET_GROUPS) {
                outfit->geoset[group] = geoset_group;
            }
        }
    }
    outfit->flags |= DbcField(item_display_info, record, flags_field);
    FOR_LOOP(i, M2TOOL_CHAR_TEX_COUNT) {
        LPCSTR texture = DbcString(item_display_info, DbcField(item_display_info, record, texture_base + i));
        if (texture && *texture) {
            outfit->texture[i] = texture;
        }
    }
}

static void AddDisplayInfoListToOutfit(m2ToolWowOutfit_t *outfit,
                                       m2ToolDbc_t const *item_display_info,
                                       DWORD const *display_ids,
                                       DWORD display_count,
                                       DWORD slot) {
    FOR_LOOP(i, display_count) {
        AddDisplayInfoToOutfit(outfit, item_display_info, display_ids[i], slot);
    }
}

typedef struct {
    DWORD display_ids[4];
} m2ToolEquipmentItem_t;

typedef struct {
    DWORD race_id;
    DWORD gender_id;
    m2ToolEquipmentItem_t items[256];
} m2ToolEquipmentSlotItems_t;

static m2ToolEquipmentItem_t const *EquipmentSlotItem(m2ToolEquipmentSlotItems_t const *lists,
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

static void AddEquipmentItemToOutfit(m2ToolWowOutfit_t *outfit,
                                     m2ToolDbc_t const *item_display_info,
                                     m2ToolEquipmentSlotItems_t const *lists,
                                     DWORD list_count,
                                     DWORD race_id,
                                     DWORD gender_id,
                                     BYTE item_index,
                                     DWORD slot) {
    m2ToolEquipmentItem_t const *item = EquipmentSlotItem(lists, list_count, race_id, gender_id, item_index);

    if (!outfit || !item_display_info || !item) {
        return;
    }
    AddDisplayInfoListToOutfit(outfit,
                               item_display_info,
                               item->display_ids,
                               sizeof(item->display_ids) / sizeof(item->display_ids[0]),
                               slot);
}

static void ApplyEquipmentItems(m2ToolWowOutfit_t *outfit,
                                m2ToolDbc_t const *item_display_info,
                                DWORD race_id,
                                DWORD gender_id,
                                DWORD equipment) {
    static m2ToolEquipmentSlotItems_t const upper_body_items[] = {
        { 2, 0, { [1] = { { 27274, 0, 0, 0 } } } }
    };
    static m2ToolEquipmentSlotItems_t const lower_body_items[] = {
        { 2, 0, { [1] = { { 27275, 0, 0, 0 } } } }
    };
    static m2ToolEquipmentSlotItems_t const hand_items[] = {
        { 2, 0, { [1] = { { 27271, 0, 0, 0 } } } }
    };
    static m2ToolEquipmentSlotItems_t const foot_items[] = {
        { 2, 0, { [1] = { { 27270, 0, 0, 0 } } } }
    };
    wowEquipment_t items = Wow_UnpackEquipment(equipment);

    AddEquipmentItemToOutfit(outfit, item_display_info, upper_body_items,
                             sizeof(upper_body_items) / sizeof(upper_body_items[0]),
                             race_id, gender_id, items.upperBodyItem, M2TOOL_SLOT_CHEST);
    AddEquipmentItemToOutfit(outfit, item_display_info, lower_body_items,
                             sizeof(lower_body_items) / sizeof(lower_body_items[0]),
                             race_id, gender_id, items.lowerBodyItem, M2TOOL_SLOT_LEGS);
    AddEquipmentItemToOutfit(outfit, item_display_info, hand_items,
                             sizeof(hand_items) / sizeof(hand_items[0]),
                             race_id, gender_id, items.handItem, M2TOOL_SLOT_GLOVES);
    AddEquipmentItemToOutfit(outfit, item_display_info, foot_items,
                             sizeof(foot_items) / sizeof(foot_items[0]),
                             race_id, gender_id, items.footItem, M2TOOL_SLOT_BOOTS);
}

static BOOL LoadWowStartOutfit(LPCSTR model_path,
                               DWORD appearance,
                               DWORD equipment,
                               m2ToolWowOutfit_t *outfit) {
    m2ToolDbc_t start = { 0 };
    m2ToolDbc_t item = { 0 };
    DWORD race_id;
    DWORD gender_id;
    DWORD class_id;
    BOOL found = false;

    if (!model_path || !outfit || !CharacterRaceGender(model_path, &race_id, &gender_id)) {
        return false;
    }
    memset(outfit, 0, sizeof(*outfit));
    class_id = WowClassFromAppearance(appearance);
    if (!LoadDbc("DBFilesClient\\CharStartOutfit.dbc", &start) ||
        !LoadDbc("DBFilesClient\\ItemDisplayInfo.dbc", &item)) {
        FreeDbc(&start);
        FreeDbc(&item);
        return false;
    }

    FOR_LOOP(i, start.records) {
        BYTE const *record = start.records_base + i * start.record_size;
        DWORD race_class_gender = DbcField(&start, record, 1);
        DWORD record_race = race_class_gender & 0xff;
        DWORD record_class = (race_class_gender >> 8) & 0xff;
        DWORD record_gender = (race_class_gender >> 16) & 0xff;

        if (record_race != race_id || record_class != class_id || record_gender != gender_id) {
            continue;
        }
        static DWORD const start_outfit_slot_map[12] = {
            M2TOOL_SLOT_HEAD, M2TOOL_SLOT_SHOULDERS, M2TOOL_SLOT_CHEST, M2TOOL_SLOT_SHIRT,
            M2TOOL_SLOT_BELT, M2TOOL_SLOT_LEGS, M2TOOL_SLOT_BOOTS, M2TOOL_SLOT_GLOVES,
            M2TOOL_SLOT_TABARD, M2TOOL_SLOT_CAPE, M2TOOL_SLOT_COUNT, M2TOOL_SLOT_COUNT
        };
        FOR_LOOP(display, 12) {
            AddDisplayInfoToOutfit(outfit, &item, DbcField(&start, record, 14 + display),
                                   start_outfit_slot_map[display]);
        }
        ApplyEquipmentItems(outfit, &item, race_id, gender_id, equipment);
        found = true;
        break;
    }

    FreeDbc(&start);
    FreeDbc(&item);
    return found;
}

static BOOL ComponentTexturePath(LPCSTR stem, BYTE slot, LPCSTR model_path, LPSTR out, DWORD out_size) {
    static LPCSTR const folders[M2TOOL_CHAR_TEX_COUNT] = {
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

    if (!stem || !*stem || slot >= M2TOOL_CHAR_TEX_COUNT || !out || out_size == 0) {
        return false;
    }
    gender_suffix = CharacterRaceGender(model_path, &race_id, &gender_id) && gender_id ? "F" : "M";
    snprintf(candidate, sizeof(candidate), "Item\\TextureComponents\\%s\\%s_%s.blp",
             folders[slot], stem, gender_suffix);
    if (Tool_FileExists(archives, sizeof(archives) / sizeof(archives[0]), candidate)) {
        snprintf(out, out_size, "%s", candidate);
        return true;
    }
    snprintf(candidate, sizeof(candidate), "Item\\TextureComponents\\%s\\%s_U.blp",
             folders[slot], stem);
    if (Tool_FileExists(archives, sizeof(archives) / sizeof(archives[0]), candidate)) {
        snprintf(out, out_size, "%s", candidate);
        return true;
    }
    snprintf(out, out_size, "Item\\TextureComponents\\%s\\%s_%s.blp",
             folders[slot], stem, gender_suffix);
    return true;
}

static BOOL WowVisibleSection(WORD section_id, m2ToolWowOutfit_t const *outfit) {
    DWORD group, geoset, expected;

    if (section_id < 400) {
        return true;
    }
    if (!outfit) {
        return section_id == 401 || section_id == 702 || section_id == 1501;
    }

    group = section_id / 100;
    geoset = (group < M2TOOL_NUM_GEOSET_GROUPS) ? outfit->geoset[group] : 0;

    switch (group) {
        case 4:  expected = 401 + geoset; break;
        case 5:  expected = 501 + geoset; break;
        case 7:  expected = 702; break;
        case 8:  expected = 800 + geoset; break;
        case 9:  expected = 900 + geoset; break;
        case 10: return false;
        case 11: return false;
        case 12: return false;
        case 13:
            if (outfit->flags & 0x4) {
                return false;
            }
            expected = 1301 + geoset;
            break;
        case 15: expected = 1501; break;
        default: return false;
    }
    return section_id == expected;
}

static BOOL TagEquals(BYTE const *tag, DWORD fourcc) {
    DWORD value;
    memcpy(&value, tag, sizeof(value));
    return value == fourcc;
}

static BOOL FindM2Payload(BYTE const *data, DWORD size, BYTE const **payload, DWORD *payload_size) {
    DWORD magic;
    DWORD offset;

    if (!data || size < sizeof(DWORD) || !payload || !payload_size) {
        return false;
    }
    memcpy(&magic, data, sizeof(magic));
    if (magic == ID_MD20) {
        *payload = data;
        *payload_size = size;
        return true;
    }
    if (magic != ID_MD21 && magic != ID_12DM) {
        return false;
    }
    offset = 0;
    while (offset + 8 <= size) {
        BYTE const *tag = data + offset;
        DWORD chunk_size;
        memcpy(&chunk_size, data + offset + 4, sizeof(chunk_size));
        offset += 8;
        if (chunk_size > size - offset) {
            return false;
        }
        if (TagEquals(tag, ID_MD20) ||
            TagEquals(tag, ID_MD21) ||
            TagEquals(tag, ID_12DM)) {
            if (chunk_size >= sizeof(DWORD)) {
                DWORD inner_magic;
                memcpy(&inner_magic, data + offset, sizeof(inner_magic));
                if (inner_magic == ID_MD20) {
                    *payload = data + offset;
                    *payload_size = chunk_size;
                    return true;
                }
            }
        }
        offset += chunk_size;
    }
    return false;
}

static DWORD ReadU32(BYTE const *p) {
    DWORD value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static float ReadFloat(BYTE const *p) {
    float value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static m2Array_t ReadArray(BYTE const *p) {
    m2Array_t array;
    memcpy(&array, p, sizeof(array));
    return array;
}

static VECTOR3 ReadVec3(BYTE const *p) {
    return (VECTOR3){ ReadFloat(p), ReadFloat(p + 4), ReadFloat(p + 8) };
}

static void SkipArray(DWORD *offset) {
    *offset += sizeof(m2Array_t);
}

static m2Array_t NextArray(BYTE const *data, DWORD *offset) {
    m2Array_t value = ReadArray(data + *offset);
    *offset += sizeof(m2Array_t);
    return value;
}

static BOOL ParseHeader(BYTE const *data, DWORD size, m2HeaderInfo_t *out) {
    DWORD offset;
    BOOL legacy;

    if (!data || size < 180 || !out || ReadU32(data) != ID_MD20) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->magic = ReadU32(data);
    out->version = ReadU32(data + 4);
    legacy = out->version <= 263;

    offset = 8;
    out->name = NextArray(data, &offset);
    out->flags = ReadU32(data + offset);
    offset += sizeof(DWORD);
    out->global_sequences = NextArray(data, &offset);
    out->animations = NextArray(data, &offset);
    out->animation_lookup = NextArray(data, &offset);
    if (legacy) {
        out->playable_animation_lookup = NextArray(data, &offset);
    }
    out->bones = NextArray(data, &offset);
    out->key_bone_lookup = NextArray(data, &offset);
    out->vertices = NextArray(data, &offset);
    if (legacy) {
        out->views = NextArray(data, &offset);
    } else {
        out->views = (m2Array_t){ (int32_t)ReadU32(data + offset), 0 };
        offset += sizeof(DWORD);
    }
    out->colors = NextArray(data, &offset);
    out->textures = NextArray(data, &offset);
    out->transparency_lookup = NextArray(data, &offset);
    if (legacy) {
        out->texture_flipbooks = NextArray(data, &offset);
    }
    out->texture_animations = NextArray(data, &offset);
    out->color_replacements = NextArray(data, &offset);
    out->render_flags = NextArray(data, &offset);
    out->bone_lookup_table = NextArray(data, &offset);
    out->texture_lookup_table = NextArray(data, &offset);
    out->texture_units = NextArray(data, &offset);
    out->transparency_lookup_table = NextArray(data, &offset);
    out->texture_animation_lookup_table = NextArray(data, &offset);

    if (offset + 7 * sizeof(float) > size) {
        return false;
    }
    out->bounding_box_min = ReadVec3(data + offset);
    offset += 12;
    out->bounding_box_max = ReadVec3(data + offset);
    offset += 12;
    out->bounding_sphere_radius = ReadFloat(data + offset);
    offset += 4;
    out->collision_box_min = ReadVec3(data + offset);
    offset += 12;
    out->collision_box_max = ReadVec3(data + offset);
    offset += 12;
    out->collision_sphere_radius = ReadFloat(data + offset);
    offset += 4;

    if (offset + 3 * sizeof(m2Array_t) <= size) {
        out->bounding_triangles = NextArray(data, &offset);
        out->bounding_vertices = NextArray(data, &offset);
        out->bounding_normals = NextArray(data, &offset);
    }
    if (offset + 8 * sizeof(m2Array_t) <= size) {
        out->attachments = NextArray(data, &offset);
        out->attachment_lookup = NextArray(data, &offset);
        out->events = NextArray(data, &offset);
        out->lights = NextArray(data, &offset);
        out->cameras = NextArray(data, &offset);
        out->camera_lookup = NextArray(data, &offset);
        out->ribbon_emitters = NextArray(data, &offset);
        out->particle_emitters = NextArray(data, &offset);
    }
    (void)SkipArray;
    return true;
}

static BOOL ArrayRange(m2Array_t array, DWORD elem_size, DWORD file_size, DWORD *offset, DWORD *bytes) {
    if (array.count <= 0 || array.offset < 0 || elem_size == 0) {
        return false;
    }
    if ((DWORD)array.count > (((DWORD)~0u) / elem_size)) {
        return false;
    }
    *offset = (DWORD)array.offset;
    *bytes = (DWORD)array.count * elem_size;
    return *offset <= file_size && *bytes <= file_size - *offset;
}

static void const *ArrayPtr(BYTE const *base, DWORD file_size, m2Array_t array, DWORD elem_size) {
    DWORD offset;
    DWORD bytes;
    if (!ArrayRange(array, elem_size, file_size, &offset, &bytes)) {
        return NULL;
    }
    return base + offset;
}

static LPCSTR StringPtr(BYTE const *base, DWORD file_size, m2Array_t array) {
    DWORD offset;
    DWORD bytes;
    if (!ArrayRange(array, 1, file_size, &offset, &bytes) || bytes == 0) {
        return NULL;
    }
    if (!memchr(base + offset, '\0', bytes)) {
        return NULL;
    }
    return (LPCSTR)(base + offset);
}

static void PrintVec3(LPCSTR label, VECTOR3 v) {
    printf("  %-22s %.6f %.6f %.6f\n", label, v.x, v.y, v.z);
}

static void PrintBoundsMetrics(LPCSTR label, VECTOR3 min, VECTOR3 max) {
    VECTOR3 extent = { max.x - min.x, max.y - min.y, max.z - min.z };
    VECTOR3 center = {
        (min.x + max.x) * 0.5f,
        (min.y + max.y) * 0.5f,
        (min.z + max.z) * 0.5f,
    };
    char extent_label[64];
    char center_label[64];

    snprintf(extent_label, sizeof(extent_label), "%s.extent", label);
    snprintf(center_label, sizeof(center_label), "%s.center", label);
    printf("  %-22s %.6f %.6f %.6f\n", extent_label, extent.x, extent.y, extent.z);
    printf("  %-22s %.6f %.6f %.6f\n", center_label, center.x, center.y, center.z);
}

static void PrintArray(LPCSTR label, m2Array_t array, BOOL count_only) {
    if (count_only) {
        printf("  %-28s count=%d\n", label, array.count);
    } else {
        printf("  %-28s count=%d offset=%d\n", label, array.count, array.offset);
    }
}

static void PrintAttachments(BYTE const *data, DWORD size, m2HeaderInfo_t const *header) {
    BYTE const *attachments;
    WORD const *lookup;
    BOOL legacy = header->version <= 263;
    DWORD stride = legacy ? sizeof(m2AttachmentClassic_t) : sizeof(m2AttachmentModern_t);

    attachments = ArrayPtr(data, size, header->attachments, stride);
    if (!attachments || header->attachments.count <= 0) {
        return;
    }

    printf("attachments:\n");
    FOR_LOOP(i, (DWORD)header->attachments.count) {
        DWORD attachment_id;
        WORD bone_index;
        VECTOR3 position;

        if (legacy) {
            m2AttachmentClassic_t const *attachment = (m2AttachmentClassic_t const *)(attachments + i * stride);
            attachment_id = attachment->attachment_id;
            bone_index = attachment->bone_index;
            position = attachment->position;
        } else {
            m2AttachmentModern_t const *attachment = (m2AttachmentModern_t const *)(attachments + i * stride);
            attachment_id = attachment->attachment_id;
            bone_index = attachment->bone_index;
            position = attachment->position;
        }

        printf("  [%03u] id=%u bone=%u pos=%.6f %.6f %.6f\n",
               (unsigned)i,
               (unsigned)attachment_id,
               (unsigned)bone_index,
               position.x,
               position.y,
               position.z);
    }

    lookup = ArrayPtr(data, size, header->attachment_lookup, sizeof(*lookup));
    if (!lookup || header->attachment_lookup.count <= 0) {
        return;
    }

    printf("attachment_lookup:\n");
    FOR_LOOP(i, (DWORD)header->attachment_lookup.count) {
        printf("  [%03u] -> %u\n", (unsigned)i, (unsigned)lookup[i]);
    }
}

static void UpdateBounds(BOX3 *bounds, VECTOR3 p, BOOL *has_bounds) {
    if (!*has_bounds) {
        bounds->min = p;
        bounds->max = p;
        *has_bounds = true;
        return;
    }
    bounds->min.x = MIN(bounds->min.x, p.x);
    bounds->min.y = MIN(bounds->min.y, p.y);
    bounds->min.z = MIN(bounds->min.z, p.z);
    bounds->max.x = MAX(bounds->max.x, p.x);
    bounds->max.y = MAX(bounds->max.y, p.y);
    bounds->max.z = MAX(bounds->max.z, p.z);
}

static BOOL CalculateVertexBounds(BYTE const *data, DWORD size, m2Array_t vertices, BOX3 *bounds) {
    m2Vertex_t const *items = ArrayPtr(data, size, vertices, sizeof(*items));
    BOOL has_bounds = false;

    if (!items || !bounds) {
        return false;
    }
    FOR_LOOP(i, (DWORD)vertices.count) {
        UpdateBounds(bounds, items[i].pos, &has_bounds);
    }
    return has_bounds;
}

static LPCSTR AnimationName(WORD id) {
    switch (id) {
        case 0: return "Stand";
        case 1: return "Death";
        case 2: return "Spell";
        case 3: return "Stop";
        case 4: return "Walk";
        case 5: return "Run";
        case 6: return "Dead";
        case 7: return "Rise";
        case 8: return "StandWound";
        case 9: return "CombatWound";
        case 10: return "CombatCritical";
        case 11: return "ShuffleLeft";
        case 12: return "ShuffleRight";
        case 13: return "WalkBackwards";
        case 14: return "Stun";
        case 15: return "HandsClosed";
        case 16: return "AttackUnarmed";
        case 17: return "Attack1H";
        case 18: return "Attack2H";
        case 19: return "Attack2HL";
        case 20: return "ParryUnarmed";
        case 21: return "Parry1H";
        case 22: return "Parry2H";
        case 23: return "Parry2HL";
        case 24: return "ShieldBlock";
        case 25: return "ReadyUnarmed";
        case 26: return "Ready1H";
        case 27: return "Ready2H";
        case 28: return "Ready2HL";
        case 29: return "ReadyBow";
        case 30: return "Dodge";
        case 37: return "JumpStart";
        case 38: return "Jump";
        case 39: return "JumpEnd";
        case 40: return "Fall";
        case 41: return "SwimIdle";
        case 42: return "Swim";
        default: return NULL;
    }
}

static void PrintAnimations(BYTE const *data, DWORD size, m2HeaderInfo_t const *header) {
    DWORD offset;
    DWORD bytes;

    if (!ArrayRange(header->animations,
                    header->version <= 263 ? sizeof(m2SequenceClassic_t) : sizeof(m2SequenceModern_t),
                    size,
                    &offset,
                    &bytes)) {
        printf("animations: unavailable\n");
        return;
    }
    printf("animations:\n");
    FOR_LOOP(i, (DWORD)header->animations.count) {
        if (header->version <= 263) {
            m2SequenceClassic_t const *seq = (m2SequenceClassic_t const *)(data + offset + i * sizeof(*seq));
            LPCSTR name = AnimationName(seq->animation_id);
            printf("  [%03u] id=%u%s%s sub=%u start=%u end=%u speed=%.3f flags=0x%08x blend=%u radius=%.3f next=%d alias=%u bounds=(%.3f %.3f %.3f)..(%.3f %.3f %.3f)\n",
                   (unsigned)i,
                   (unsigned)seq->animation_id,
                   name ? " " : "",
                   name ? name : "",
                   (unsigned)seq->sub_animation_id,
                   (unsigned)seq->start_timestamp,
                   (unsigned)seq->end_timestamp,
                   seq->movement_speed,
                   (unsigned)seq->flags,
                   (unsigned)seq->blend_time,
                   seq->radius,
                   (int)seq->next_animation,
                   (unsigned)seq->alias_next,
                   seq->min.x, seq->min.y, seq->min.z,
                   seq->max.x, seq->max.y, seq->max.z);
        } else {
            m2SequenceModern_t const *seq = (m2SequenceModern_t const *)(data + offset + i * sizeof(*seq));
            LPCSTR name = AnimationName(seq->animation_id);
            printf("  [%03u] id=%u%s%s sub=%u length=%u speed=%.3f flags=0x%08x blend=%u radius=%.3f next=%d alias=%u bounds=(%.3f %.3f %.3f)..(%.3f %.3f %.3f)\n",
                   (unsigned)i,
                   (unsigned)seq->animation_id,
                   name ? " " : "",
                   name ? name : "",
                   (unsigned)seq->sub_animation_id,
                   (unsigned)seq->length,
                   seq->movement_speed,
                   (unsigned)seq->flags,
                   (unsigned)seq->blend_time,
                   seq->radius,
                   (int)seq->next_animation,
                   (unsigned)seq->alias_next,
                   seq->min.x, seq->min.y, seq->min.z,
                   seq->max.x, seq->max.y, seq->max.z);
        }
    }
}

static DWORD SequenceStart(BYTE const *sequence, BOOL classic) {
    return classic ? ((m2SequenceClassic_t const *)sequence)->start_timestamp : 0;
}

static DWORD SequenceDuration(BYTE const *sequence, BOOL classic) {
    if (classic) {
        m2SequenceClassic_t const *seq = (m2SequenceClassic_t const *)sequence;
        return seq->end_timestamp > seq->start_timestamp ? seq->end_timestamp - seq->start_timestamp : 0;
    }
    return ((m2SequenceModern_t const *)sequence)->length;
}

static WORD SequenceAnimationId(BYTE const *sequence, BOOL classic) {
    return classic
        ? ((m2SequenceClassic_t const *)sequence)->animation_id
        : ((m2SequenceModern_t const *)sequence)->animation_id;
}

static BOOL ParseSequenceSelector(LPCSTR selector, DWORD max_count, DWORD *out_index) {
    char *end = NULL;
    unsigned long value;

    if (!selector || !*selector || !out_index) {
        return false;
    }
    value = strtoul(selector, &end, 10);
    if (end && *end == '\0' && value < max_count) {
        *out_index = (DWORD)value;
        return true;
    }
    return false;
}

static BOOL FindSequenceByName(BYTE const *sequences,
                               DWORD sequence_count,
                               DWORD sequence_stride,
                               BOOL classic,
                               LPCSTR selector,
                               DWORD *out_index) {
    if (ParseSequenceSelector(selector, sequence_count, out_index)) {
        return true;
    }
    FOR_LOOP(i, sequence_count) {
        BYTE const *sequence = sequences + i * sequence_stride;
        LPCSTR name = AnimationName(SequenceAnimationId(sequence, classic));
        if (name && !strcasecmp(name, selector)) {
            *out_index = i;
            return true;
        }
    }
    return false;
}

static DWORD ClassicTrackKeyRange(BYTE const *data,
                                  DWORD size,
                                  m2TrackClassic_t const *track,
                                  DWORD sequence_index,
                                  DWORD elem_size,
                                  m2Range_t *out_range,
                                  DWORD const **out_times,
                                  BYTE const **out_keys) {
    m2Range_t const *ranges;
    m2Range_t range;
    DWORD const *times;
    BYTE const *keys;
    DWORD count;

    if (!track || !out_range || !out_times || !out_keys) {
        return 0;
    }
    ranges = ArrayPtr(data, size, track->ranges, sizeof(*ranges));
    if (!ranges || track->ranges.count <= 0) {
        return 0;
    }
    if (sequence_index >= (DWORD)track->ranges.count) {
        sequence_index = 0;
    }
    range = ranges[sequence_index];
    if (range.end < range.start) {
        return 0;
    }
    times = ArrayPtr(data, size, track->times, sizeof(*times));
    keys = ArrayPtr(data, size, track->keys, elem_size);
    if (!times || !keys ||
        range.start >= (DWORD)track->times.count ||
        range.start >= (DWORD)track->keys.count) {
        return 0;
    }
    count = range.end - range.start + 1;
    count = MIN(count, (DWORD)track->times.count - range.start);
    count = MIN(count, (DWORD)track->keys.count - range.start);
    if (count == 0) {
        return 0;
    }
    *out_range = range;
    *out_times = times + range.start;
    *out_keys = keys + range.start * elem_size;
    return count;
}

static DWORD ModernTrackKeyRange(BYTE const *data,
                                 DWORD size,
                                 m2Track_t const *track,
                                 DWORD sequence_index,
                                 DWORD elem_size,
                                 DWORD const **out_times,
                                 BYTE const **out_keys) {
    m2SequenceTimes_t const *sequence_times;
    m2SequenceKeys_t const *sequence_keys;
    DWORD const *times;
    BYTE const *keys;

    if (!track || !out_times || !out_keys) {
        return 0;
    }
    sequence_times = ArrayPtr(data, size, track->sequence_times, sizeof(*sequence_times));
    sequence_keys = ArrayPtr(data, size, track->sequence_keys, sizeof(*sequence_keys));
    if (!sequence_times || !sequence_keys ||
        track->sequence_times.count <= 0 ||
        track->sequence_keys.count <= 0) {
        return 0;
    }
    if (sequence_index >= (DWORD)track->sequence_times.count ||
        sequence_index >= (DWORD)track->sequence_keys.count) {
        sequence_index = 0;
    }
    times = ArrayPtr(data, size, sequence_times[sequence_index].times, sizeof(*times));
    keys = ArrayPtr(data, size, sequence_keys[sequence_index].keys, elem_size);
    if (!times || !keys) {
        return 0;
    }
    *out_times = times;
    *out_keys = keys;
    return MIN((DWORD)sequence_times[sequence_index].times.count,
               (DWORD)sequence_keys[sequence_index].keys.count);
}

static VECTOR3 KeyVec3(BYTE const *keys, DWORD index) {
    return *(VECTOR3 const *)(keys + index * sizeof(VECTOR3));
}

static void PrintTrackLine(LPCSTR label,
                           DWORD bone_index,
                           DWORD flags,
                           WORD parent_index,
                           VECTOR3 pivot,
                           DWORD count,
                           m2Range_t range,
                           DWORD const *times,
                           BYTE const *keys,
                           DWORD elem_size,
                           BOOL classic,
                           BOOL vector_keys) {
    printf("  bone[%03u] %-5s flags=0x%08x parent=%u pivot=(%.3f %.3f %.3f) keys=%u",
           (unsigned)bone_index,
           label,
           (unsigned)flags,
           (unsigned)parent_index,
           pivot.x, pivot.y, pivot.z,
           (unsigned)count);
    if (classic) {
        printf(" range=%u..%u", (unsigned)range.start, (unsigned)range.end);
    }
    if (count && times) {
        printf(" time=%u..%u", (unsigned)times[0], (unsigned)times[count - 1]);
    }
    if (count && keys) {
        if (vector_keys) {
            VECTOR3 first = KeyVec3(keys, 0);
            VECTOR3 mid = KeyVec3(keys, count / 2);
            VECTOR3 last = KeyVec3(keys, count - 1);
            printf(" first=(%.3f %.3f %.3f) mid=(%.3f %.3f %.3f) last=(%.3f %.3f %.3f)",
                   first.x, first.y, first.z,
                   mid.x, mid.y, mid.z,
                   last.x, last.y, last.z);
        } else if (elem_size == sizeof(QUATERNION)) {
            QUATERNION const *first = (QUATERNION const *)keys;
            QUATERNION const *mid = (QUATERNION const *)(keys + (count / 2) * elem_size);
            QUATERNION const *last = (QUATERNION const *)(keys + (count - 1) * elem_size);
            printf(" firstQuat=(%.6f %.6f %.6f %.6f) midQuat=(%.6f %.6f %.6f %.6f) lastQuat=(%.6f %.6f %.6f %.6f)",
                   first->x, first->y, first->z, first->w,
                   mid->x, mid->y, mid->z, mid->w,
                   last->x, last->y, last->z, last->w);
        } else if (elem_size == sizeof(m2CompQuat_t)) {
            m2CompQuat_t const *first = (m2CompQuat_t const *)keys;
            m2CompQuat_t const *last = (m2CompQuat_t const *)(keys + (count - 1) * elem_size);
            printf(" firstQuatRaw=(%08x %08x) lastQuatRaw=(%08x %08x)",
                   (unsigned)first->auCompQ[0],
                   (unsigned)first->auCompQ[1],
                   (unsigned)last->auCompQ[0],
                   (unsigned)last->auCompQ[1]);
        }
    }
    printf("\n");
}

static void PrintAnimationDiagnostics(BYTE const *data, DWORD size, m2HeaderInfo_t const *header, LPCSTR selector) {
    DWORD sequence_offset;
    DWORD sequence_bytes;
    BYTE const *sequences;
    BYTE const *sequence;
    DWORD sequence_stride;
    DWORD sequence_index;
    BOOL classic = header->version <= 263;
    DWORD bone_stride = classic ? sizeof(m2CompBoneClassic_t) : sizeof(m2CompBoneModern_t);
    BYTE const *bones;
    DWORD bones_offset;
    DWORD bones_bytes;
    DWORD sequence_count;
    DWORD trans_bones = 0;
    DWORD rot_bones = 0;
    DWORD scale_bones = 0;
    DWORD printed = 0;

    sequence_stride = classic ? sizeof(m2SequenceClassic_t) : sizeof(m2SequenceModern_t);
    if (!ArrayRange(header->animations, sequence_stride, size, &sequence_offset, &sequence_bytes)) {
        printf("anim_debug: animations unavailable\n");
        return;
    }
    if (!ArrayRange(header->bones, bone_stride, size, &bones_offset, &bones_bytes)) {
        printf("anim_debug: bones unavailable for stride=%u\n", (unsigned)bone_stride);
        return;
    }
    sequences = data + sequence_offset;
    bones = data + bones_offset;
    sequence_count = sequence_bytes / sequence_stride;
    if (!FindSequenceByName(sequences, sequence_count, sequence_stride, classic, selector, &sequence_index)) {
        printf("anim_debug: sequence '%s' not found\n", selector);
        return;
    }
    sequence = sequences + sequence_index * sequence_stride;
    printf("anim_debug: selector=%s seq=%u id=%u%s%s layout=%s start=%u duration=%u bones=%d bone_stride=%u\n",
           selector,
           (unsigned)sequence_index,
           (unsigned)SequenceAnimationId(sequence, classic),
           AnimationName(SequenceAnimationId(sequence, classic)) ? " " : "",
           AnimationName(SequenceAnimationId(sequence, classic)) ? AnimationName(SequenceAnimationId(sequence, classic)) : "",
           classic ? "classic" : "modern",
           (unsigned)SequenceStart(sequence, classic),
           (unsigned)SequenceDuration(sequence, classic),
           header->bones.count,
           (unsigned)bone_stride);

    FOR_LOOP(i, (DWORD)header->bones.count) {
        DWORD const *times = NULL;
        BYTE const *keys = NULL;
        DWORD trans_count = 0;
        DWORD rot_count = 0;
        DWORD scale_count = 0;
        m2Range_t trans_range = { 0, 0 };
        m2Range_t rot_range = { 0, 0 };
        m2Range_t scale_range = { 0, 0 };
        DWORD flags;
        WORD parent_index;
        VECTOR3 pivot;

        if (classic) {
            m2CompBoneClassic_t const *bone = (m2CompBoneClassic_t const *)(bones + i * bone_stride);
            flags = bone->flags;
            parent_index = bone->parent_index;
            pivot = bone->pivot;
            trans_count = ClassicTrackKeyRange(data, size, &bone->translation_track, sequence_index, sizeof(VECTOR3), &trans_range, &times, &keys);
            if (trans_count) {
                trans_bones++;
            }
            times = NULL;
            keys = NULL;
            rot_count = ClassicTrackKeyRange(data, size, &bone->rotation_track, sequence_index, sizeof(QUATERNION), &rot_range, &times, &keys);
            if (rot_count) {
                rot_bones++;
            }
            times = NULL;
            keys = NULL;
            scale_count = ClassicTrackKeyRange(data, size, &bone->scale_track, sequence_index, sizeof(VECTOR3), &scale_range, &times, &keys);
            if (scale_count) {
                scale_bones++;
            }
        } else {
            m2CompBoneModern_t const *bone = (m2CompBoneModern_t const *)(bones + i * bone_stride);
            flags = bone->flags;
            parent_index = bone->parent_index;
            pivot = bone->pivot;
            trans_count = ModernTrackKeyRange(data, size, &bone->translation_track, sequence_index, sizeof(VECTOR3), &times, &keys);
            if (trans_count) {
                trans_bones++;
            }
            times = NULL;
            keys = NULL;
            rot_count = ModernTrackKeyRange(data, size, &bone->rotation_track, sequence_index, sizeof(m2CompQuat_t), &times, &keys);
            if (rot_count) {
                rot_bones++;
            }
            times = NULL;
            keys = NULL;
            scale_count = ModernTrackKeyRange(data, size, &bone->scale_track, sequence_index, sizeof(VECTOR3), &times, &keys);
            if (scale_count) {
                scale_bones++;
            }
        }

        if (printed < 16 && (trans_count || rot_count || scale_count || i == 0 || i == 22)) {
            if (classic) {
                m2CompBoneClassic_t const *bone = (m2CompBoneClassic_t const *)(bones + i * bone_stride);
                if (trans_count) {
                    trans_count = ClassicTrackKeyRange(data, size, &bone->translation_track, sequence_index, sizeof(VECTOR3), &trans_range, &times, &keys);
                    PrintTrackLine("trans", i, flags, parent_index, pivot, trans_count, trans_range, times, keys, sizeof(VECTOR3), true, true);
                }
                if (rot_count) {
                    rot_count = ClassicTrackKeyRange(data, size, &bone->rotation_track, sequence_index, sizeof(QUATERNION), &rot_range, &times, &keys);
                    PrintTrackLine("rot", i, flags, parent_index, pivot, rot_count, rot_range, times, keys, sizeof(QUATERNION), true, false);
                }
                if (scale_count) {
                    scale_count = ClassicTrackKeyRange(data, size, &bone->scale_track, sequence_index, sizeof(VECTOR3), &scale_range, &times, &keys);
                    PrintTrackLine("scale", i, flags, parent_index, pivot, scale_count, scale_range, times, keys, sizeof(VECTOR3), true, true);
                }
            } else {
                m2CompBoneModern_t const *bone = (m2CompBoneModern_t const *)(bones + i * bone_stride);
                if (trans_count) {
                    trans_count = ModernTrackKeyRange(data, size, &bone->translation_track, sequence_index, sizeof(VECTOR3), &times, &keys);
                    PrintTrackLine("trans", i, flags, parent_index, pivot, trans_count, trans_range, times, keys, sizeof(VECTOR3), false, true);
                }
                if (rot_count) {
                    rot_count = ModernTrackKeyRange(data, size, &bone->rotation_track, sequence_index, sizeof(m2CompQuat_t), &times, &keys);
                    PrintTrackLine("rot", i, flags, parent_index, pivot, rot_count, rot_range, times, keys, sizeof(m2CompQuat_t), false, false);
                }
                if (scale_count) {
                    scale_count = ModernTrackKeyRange(data, size, &bone->scale_track, sequence_index, sizeof(VECTOR3), &times, &keys);
                    PrintTrackLine("scale", i, flags, parent_index, pivot, scale_count, scale_range, times, keys, sizeof(VECTOR3), false, true);
                }
            }
            if (!trans_count && !rot_count && !scale_count) {
                printf("  bone[%03u] none  flags=0x%08x parent=%u pivot=(%.3f %.3f %.3f) keys=0\n",
                       (unsigned)i,
                       (unsigned)flags,
                       (unsigned)parent_index,
                       pivot.x, pivot.y, pivot.z);
            }
            printed++;
        }
    }
    printf("anim_debug_summary: trans_bones=%u rot_bones=%u scale_bones=%u\n",
           (unsigned)trans_bones,
           (unsigned)rot_bones,
           (unsigned)scale_bones);
}

static LPCSTR TextureTypeName(DWORD type) {
    switch (type) {
        case 0: return "hardcoded";
        case 1: return "body/skin";
        case 2: return "object skin";
        case 6: return "hair/beard";
        case 8: return "tauren mane";
        case 11: return "skin extra";
        default: return "unknown";
    }
}

static void PrintTextures(BYTE const *data, DWORD size, m2HeaderInfo_t const *header) {
    m2Texture_t const *textures = ArrayPtr(data, size, header->textures, sizeof(*textures));

    if (!textures) {
        printf("textures: unavailable\n");
        return;
    }
    printf("textures:\n");
    FOR_LOOP(i, (DWORD)header->textures.count) {
        m2Texture_t const *texture = textures + i;
        LPCSTR path = StringPtr(data, size, texture->filename);
        printf("  [%03u] type=%u (%s) flags=0x%08x path=%s\n",
               (unsigned)i,
               (unsigned)texture->type,
               TextureTypeName(texture->type),
               (unsigned)texture->flags,
               path && *path ? path : "<replaceable>");
    }
}

static void PrintTextureLookup(BYTE const *data, DWORD size, m2HeaderInfo_t const *header) {
    SHORT const *texture_lookup;

    if (!header || !g_dump_all || header->texture_lookup_table.count <= 0) {
        return;
    }

    texture_lookup = ArrayPtr(data, size, header->texture_lookup_table, sizeof(*texture_lookup));
    if (!texture_lookup) {
        printf("texture_lookup_table: unavailable\n");
        return;
    }

    printf("texture_lookup_table values:");
    FOR_LOOP(i, (DWORD)header->texture_lookup_table.count) {
        printf(" %d", (int)texture_lookup[i]);
    }
    printf("\n");
}

static void PrintBatches(BYTE const *m2_data,
                         DWORD m2_size,
                         m2HeaderInfo_t const *header,
                         BYTE const *skin_data,
                         DWORD skin_size,
                         m2Array_t sections_array,
                         m2Array_t batches_array,
                         BOOL legacy_sections,
                         LPCSTR label) {
    m2Batch_t const *batches;
    SHORT const *texture_lookup;

    if (!g_dump_all || !header || batches_array.count <= 0) {
        return;
    }

    batches = ArrayPtr(skin_data, skin_size, batches_array, sizeof(*batches));
    if (!batches) {
        printf("%s.batches: unavailable\n", label);
        return;
    }

    texture_lookup = ArrayPtr(m2_data, m2_size, header->texture_lookup_table, sizeof(*texture_lookup));
    printf("%s.batches detail:\n", label);
    FOR_LOOP(i, (DWORD)batches_array.count) {
        m2Batch_t const *batch = batches + i;
        SHORT texture_index = -1;
        WORD skin_section_id = 0xffff;
        if (texture_lookup && batch->texture_combo_index < (WORD)header->texture_lookup_table.count) {
            texture_index = texture_lookup[batch->texture_combo_index];
        }
        if (batch->skin_section_index < (WORD)sections_array.count) {
            if (legacy_sections) {
                m2SkinSectionLegacy_t const *sections = ArrayPtr(skin_data,
                                                                 skin_size,
                                                                 sections_array,
                                                                 sizeof(*sections));
                if (sections) {
                    skin_section_id = sections[batch->skin_section_index].skin_section_id;
                }
            } else {
                m2SkinSection_t const *sections = ArrayPtr(skin_data,
                                                          skin_size,
                                                          sections_array,
                                                          sizeof(*sections));
                if (sections) {
                    skin_section_id = sections[batch->skin_section_index].skin_section_id;
                }
            }
        }
        printf("  [%03u] section=%u section_id=%u geoset=%u material=%u layer=%u tex_count=%u tex_combo=%u tex_index=%d coord=%u weight=%u transform=%u flags=0x%02x shader=0x%04x\n",
               (unsigned)i,
               (unsigned)batch->skin_section_index,
               (unsigned)skin_section_id,
               (unsigned)batch->geoset_index,
               (unsigned)batch->material_index,
               (unsigned)batch->material_layer,
               (unsigned)batch->texture_count,
               (unsigned)batch->texture_combo_index,
               (int)texture_index,
               (unsigned)batch->texture_coord_combo_index,
               (unsigned)batch->texture_weight_combo_index,
               (unsigned)batch->texture_transform_combo_index,
               (unsigned)batch->flags,
               (unsigned)batch->shader_id);
    }
}

static BOOL DefaultSkinPath(LPCSTR model_path, LPSTR out, DWORD out_size) {
    return CopyWithExtension(model_path, "00.skin", out, out_size);
}

static void PrintEmbeddedSkinInfo(BYTE const *data, DWORD size, m2HeaderInfo_t const *header) {
    m2EmbeddedView_t const *views;

    if (!header || header->version > 263) {
        return;
    }
    views = ArrayPtr(data, size, header->views, sizeof(*views));
    if (!views) {
        printf("embedded_skins: unavailable\n");
        return;
    }
    printf("embedded_skins: count=%d\n", header->views.count);
    FOR_LOOP(i, (DWORD)header->views.count) {
        m2EmbeddedView_t const *view = views + i;
        printf("  view[%u]\n", (unsigned)i);
        PrintArray("view.vertices", view->vertices, false);
        PrintArray("view.indices", view->indices, false);
        PrintArray("view.bones", view->bones, false);
        PrintArray("view.sections", view->sections, false);
        PrintArray("view.batches", view->batches, false);
        printf("  %-28s %u\n", "view.bone_count_max", (unsigned)view->bone_count_max);
        PrintBatches(data, size, header, data, size, view->sections, view->batches, true, "view");
    }
}

static void PrintSkinInfo(LPCSTR model_path, BYTE const *m2_data, DWORD m2_size, m2HeaderInfo_t const *header) {
    PATHSTR requested;
    PATHSTR resolved = { 0 };
    DWORD size = 0;
    LPBYTE data;
    m2SkinHeader_t const *skin;

    if (g_skin_path) {
        snprintf(requested, sizeof(requested), "%s", g_skin_path);
    } else if (!DefaultSkinPath(model_path, requested, sizeof(requested))) {
        printf("skin: unavailable (could not form skin path)\n");
        return;
    }

    data = ReadWholeFile(requested, &size, resolved, sizeof(resolved));
    if (!data) {
        printf("skin: not found (%s)\n", requested);
        PrintEmbeddedSkinInfo(m2_data, m2_size, header);
        return;
    }
    if (size < sizeof(*skin)) {
        printf("skin: %s too small (%u bytes)\n", resolved, (unsigned)size);
        Tool_MemFree(data);
        return;
    }
    skin = (m2SkinHeader_t const *)data;
    if (skin->magic == MAKEFOURCC('S', 'K', 'I', 'N')) {
        printf("skin: %s size=%u\n", resolved, (unsigned)size);
        PrintArray("skin.vertices", skin->vertices, false);
        PrintArray("skin.indices", skin->indices, false);
        PrintArray("skin.bones", skin->bones, false);
        PrintArray("skin.sections", skin->sections, false);
        PrintArray("skin.batches", skin->batches, false);
        printf("  %-28s %u\n", "bone_count_max", (unsigned)skin->bone_count_max);
        PrintBatches(m2_data, m2_size, header, data, size, skin->sections, skin->batches, false, "skin");
    } else {
        printf("skin: %s has unexpected magic %.4s\n", resolved, (char const *)&skin->magic);
    }
    Tool_MemFree(data);
}

static void AddSectionId(WORD *sections, DWORD *count, DWORD max_count, WORD section_id) {
    if (!sections || !count) {
        return;
    }
    FOR_LOOP(i, *count) {
        if (sections[i] == section_id) {
            return;
        }
    }
    if (*count < max_count) {
        sections[(*count)++] = section_id;
    }
}

static DWORD CollectSkinSections(LPCSTR model_path,
                                 BYTE const *m2_data,
                                 DWORD m2_size,
                                 m2HeaderInfo_t const *header,
                                 WORD *sections,
                                 DWORD max_sections) {
    PATHSTR requested;
    PATHSTR resolved = { 0 };
    DWORD size = 0;
    LPBYTE data;
    m2SkinHeader_t const *skin;
    DWORD count = 0;

    if (!header || !sections || max_sections == 0) {
        return 0;
    }
    if (g_skin_path) {
        snprintf(requested, sizeof(requested), "%s", g_skin_path);
    } else {
        DefaultSkinPath(model_path, requested, sizeof(requested));
    }
    data = ReadWholeFile(requested, &size, resolved, sizeof(resolved));
    if (data && size >= sizeof(*skin)) {
        skin = (m2SkinHeader_t const *)data;
        if (skin->magic == MAKEFOURCC('S', 'K', 'I', 'N')) {
            m2SkinSection_t const *skin_sections = ArrayPtr(data, size, skin->sections, sizeof(*skin_sections));
            if (skin_sections) {
                FOR_LOOP(i, (DWORD)skin->sections.count) {
                    AddSectionId(sections, &count, max_sections, skin_sections[i].skin_section_id);
                }
            }
        }
        Tool_MemFree(data);
        return count;
    }
    SAFE_DELETE(data, Tool_MemFree);

    if (header->version <= 263) {
        m2EmbeddedView_t const *views = ArrayPtr(m2_data, m2_size, header->views, sizeof(*views));
        if (views && header->views.count > 0) {
            m2EmbeddedView_t const *view = views;
            m2SkinSectionLegacy_t const *skin_sections = ArrayPtr(m2_data, m2_size, view->sections, sizeof(*skin_sections));
            if (skin_sections) {
                FOR_LOOP(i, (DWORD)view->sections.count) {
                    AddSectionId(sections, &count, max_sections, skin_sections[i].skin_section_id);
                }
            }
        }
    }
    return count;
}

static void SortSections(WORD *sections, DWORD count) {
    for (DWORD i = 1; i < count; i++) {
        WORD value = sections[i];
        DWORD j = i;
        while (j > 0 && sections[j - 1] > value) {
            sections[j] = sections[j - 1];
            j--;
        }
        sections[j] = value;
    }
}

static void PrintWowPlayerConfig(BYTE const *m2_data, DWORD m2_size, m2HeaderInfo_t const *header) {
    static LPCSTR const slot_names[M2TOOL_CHAR_TEX_COUNT] = {
        "ArmUpper",
        "ArmLower",
        "Hand",
        "TorsoUpper",
        "TorsoLower",
        "LegUpper",
        "LegLower",
        "Foot"
    };
    m2ToolWowOutfit_t outfit;
    WORD sections[256];
    DWORD section_count;

    if (!g_wow_player_config) {
        return;
    }
    printf("wow_player_config:\n");
    printf("  appearance=%u equipment=%u class=%u\n",
           (unsigned)g_wow_appearance,
           (unsigned)g_wow_equipment,
           (unsigned)WowClassFromAppearance(g_wow_appearance));
    if (!LoadWowStartOutfit(g_model_path, g_wow_appearance, g_wow_equipment, &outfit)) {
        printf("  start_outfit: unavailable (need dbc.MPQ in -mpq list)\n");
        return;
    }
    printf("  start_display_ids:");
    FOR_LOOP(i, outfit.display_count) {
        printf(" %u", (unsigned)outfit.display_ids[i]);
    }
    printf("\n");
    printf("  item_flags=0x%08x\n", (unsigned)outfit.flags);
    printf("  geoset_groups:");
    FOR_LOOP(i, M2TOOL_NUM_GEOSET_GROUPS) {
        if (outfit.geoset[i]) {
            printf(" [%u]=%u", i, (unsigned)outfit.geoset[i]);
        }
    }
    printf("\n");
    printf("  component_textures:\n");
    FOR_LOOP(i, M2TOOL_CHAR_TEX_COUNT) {
        PATHSTR path;
        if (ComponentTexturePath(outfit.texture[i], (BYTE)i, g_model_path, path, sizeof(path))) {
            printf("    %-11s stem=%s path=%s %s\n",
                   slot_names[i],
                   outfit.texture[i],
                   path,
                   Tool_FileExists(archives, sizeof(archives) / sizeof(archives[0]), path) ? "exists" : "missing");
        }
    }

    section_count = CollectSkinSections(g_model_path, m2_data, m2_size, header, sections, sizeof(sections) / sizeof(sections[0]));
    SortSections(sections, section_count);
    printf("  visible_section_ids:");
    FOR_LOOP(i, section_count) {
        if (WowVisibleSection(sections[i], &outfit)) {
            printf(" %u", (unsigned)sections[i]);
        }
    }
    printf("\n");
    printf("  hidden_section_ids:");
    FOR_LOOP(i, section_count) {
        if (!WowVisibleSection(sections[i], &outfit)) {
            printf(" %u", (unsigned)sections[i]);
        }
    }
    printf("\n");
    printf("  note: component textures are composed into the body skin; they should not directly select clothing overlay sections.\n");
}

static void PrintHeaderArrays(m2HeaderInfo_t const *h) {
    BOOL legacy = h->version <= 263;

    printf("arrays:\n");
    PrintArray("name", h->name, false);
    PrintArray("global_sequences", h->global_sequences, false);
    PrintArray("animations", h->animations, false);
    PrintArray("animation_lookup", h->animation_lookup, false);
    if (legacy) {
        PrintArray("playable_animation_lookup", h->playable_animation_lookup, false);
    }
    PrintArray("bones", h->bones, false);
    PrintArray("key_bone_lookup", h->key_bone_lookup, false);
    PrintArray("vertices", h->vertices, false);
    PrintArray(legacy ? "views" : "skin_profiles", h->views, !legacy);
    PrintArray("colors", h->colors, false);
    PrintArray("textures", h->textures, false);
    PrintArray("transparency_lookup", h->transparency_lookup, false);
    if (legacy) {
        PrintArray("texture_flipbooks", h->texture_flipbooks, false);
    }
    PrintArray("texture_animations", h->texture_animations, false);
    PrintArray("color_replacements", h->color_replacements, false);
    PrintArray("render_flags", h->render_flags, false);
    PrintArray("bone_lookup_table", h->bone_lookup_table, false);
    PrintArray("texture_lookup_table", h->texture_lookup_table, false);
    PrintArray("texture_units", h->texture_units, false);
    PrintArray("transparency_lookup_table", h->transparency_lookup_table, false);
    PrintArray("texture_animation_lookup", h->texture_animation_lookup_table, false);
    PrintArray("bounding_triangles", h->bounding_triangles, false);
    PrintArray("bounding_vertices", h->bounding_vertices, false);
    PrintArray("bounding_normals", h->bounding_normals, false);
    PrintArray("attachments", h->attachments, false);
    PrintArray("attachment_lookup", h->attachment_lookup, false);
    PrintArray("events", h->events, false);
    PrintArray("lights", h->lights, false);
    PrintArray("cameras", h->cameras, false);
    PrintArray("camera_lookup", h->camera_lookup, false);
    PrintArray("ribbon_emitters", h->ribbon_emitters, false);
    PrintArray("particle_emitters", h->particle_emitters, false);
}

static void InspectModel(void) {
    PATHSTR resolved = { 0 };
    DWORD file_size = 0;
    LPBYTE file_data;
    BYTE const *payload;
    DWORD payload_size;
    m2HeaderInfo_t header;
    LPCSTR name;
    BOX3 vertex_bounds;
    BOOL has_vertex_bounds;
    FLOAT header_ground_offset;
    FLOAT vertex_ground_offset = 0.0f;

    file_data = ReadWholeFile(g_model_path, &file_size, resolved, sizeof(resolved));
    if (!file_data) {
        errorf("m2tool: failed to open model %s", g_model_path);
    }
    if (!FindM2Payload(file_data, file_size, &payload, &payload_size)) {
        Tool_MemFree(file_data);
        errorf("m2tool: %s is not an M2/MD21 file", resolved);
    }
    if (!ParseHeader(payload, payload_size, &header)) {
        Tool_MemFree(file_data);
        errorf("m2tool: failed to parse M2 header for %s", resolved);
    }

    name = StringPtr(payload, payload_size, header.name);
    if (!g_wow_player_config_only) {
        printf("m2tool: model=%s resolved=%s file_size=%u payload_size=%u\n",
               g_model_path,
               resolved,
               (unsigned)file_size,
               (unsigned)payload_size);
        printf("header: magic=%.4s version=%u flags=0x%08x layout=%s name=%s\n",
               (char const *)&header.magic,
               (unsigned)header.version,
               (unsigned)header.flags,
               header.version <= 263 ? "classic/tbc" : "wotlk+",
               name && *name ? name : "<none>");

        printf("bounds:\n");
        PrintVec3("bounding.min", header.bounding_box_min);
        PrintVec3("bounding.max", header.bounding_box_max);
        PrintBoundsMetrics("bounding", header.bounding_box_min, header.bounding_box_max);
        printf("  %-22s %.6f\n", "bounding.radius", header.bounding_sphere_radius);
        PrintVec3("collision.min", header.collision_box_min);
        PrintVec3("collision.max", header.collision_box_max);
        PrintBoundsMetrics("collision", header.collision_box_min, header.collision_box_max);
        printf("  %-22s %.6f\n", "collision.radius", header.collision_sphere_radius);

        has_vertex_bounds = CalculateVertexBounds(payload, payload_size, header.vertices, &vertex_bounds);
        if (has_vertex_bounds) {
            printf("vertex_bounds:\n");
            PrintVec3("vertices.min", vertex_bounds.min);
            PrintVec3("vertices.max", vertex_bounds.max);
            PrintBoundsMetrics("vertices", vertex_bounds.min, vertex_bounds.max);
            vertex_ground_offset = vertex_bounds.min.z < 0.0f ? -vertex_bounds.min.z : 0.0f;
        } else {
            printf("vertex_bounds: unavailable\n");
        }

        header_ground_offset = header.bounding_box_min.z < 0.0f ? -header.bounding_box_min.z : 0.0f;
        printf("placement:\n");
        printf("  %-30s %.6f\n", "header_bbox_ground_offset", header_ground_offset);
        if (has_vertex_bounds) {
            printf("  %-30s %.6f\n", "vertex_rest_ground_offset", vertex_ground_offset);
            printf("  %-30s %.6f\n", "header_minus_vertex_delta", header_ground_offset - vertex_ground_offset);
            printf("  %-30s %s\n", "recommended_ground_source", "vertex/rest bounds");
            printf("  %-30s %.6f\n", "recommended_ground_offset", vertex_ground_offset);
            if (header_ground_offset - vertex_ground_offset > 0.05f) {
                printf("  %-30s %s\n",
                       "diagnostic",
                       "header bbox extends below rest mesh; using it as foot contact will make the model hover");
            }
        } else {
            printf("  %-30s %s\n", "vertex_rest_ground_offset", "unavailable");
            printf("  %-30s %s\n", "recommended_ground_source", "none; keep origin on terrain");
        }

        PrintHeaderArrays(&header);
        PrintAttachments(payload, payload_size, &header);
        PrintTextures(payload, payload_size, &header);
        PrintTextureLookup(payload, payload_size, &header);
        PrintSkinInfo(resolved, payload, payload_size, &header);
    }
    PrintWowPlayerConfig(payload, payload_size, &header);
    if (!g_wow_player_config_only && g_dump_all) {
        PrintAnimations(payload, payload_size, &header);
    }
    if (!g_wow_player_config_only && g_anim_check) {
        PrintAnimationDiagnostics(payload, payload_size, &header, g_anim_check);
    }

    Tool_MemFree(file_data);
}

static BOOL LoadPreviewBounds(BOX3 *bounds, FLOAT *extent_out) {
    PATHSTR resolved = { 0 };
    DWORD file_size = 0;
    LPBYTE file_data;
    BYTE const *payload;
    DWORD payload_size;
    m2HeaderInfo_t header;
    FLOAT width;
    FLOAT depth;
    FLOAT height;

    if (!bounds || !extent_out) {
        return false;
    }
    file_data = ReadWholeFile(g_model_path, &file_size, resolved, sizeof(resolved));
    if (!file_data) {
        return false;
    }
    if (!FindM2Payload(file_data, file_size, &payload, &payload_size) ||
        !ParseHeader(payload, payload_size, &header) ||
        !CalculateVertexBounds(payload, payload_size, header.vertices, bounds)) {
        Tool_MemFree(file_data);
        return false;
    }

    width = fabsf(bounds->max.x - bounds->min.x);
    depth = fabsf(bounds->max.y - bounds->min.y);
    height = fabsf(bounds->max.z - bounds->min.z);
    *extent_out = MAX(width, MAX(depth, height));
    Tool_MemFree(file_data);
    return true;
}

static void RenderViewerFrame(refExport_t const *re, LPMODEL model, DWORD now, LPCBOX3 bounds) {
    viewDef_t viewdef = { 0 };
    renderEntity_t entity = { 0 };
    size2_t window = re->GetWindowSize();
    FLOAT aspect = window.height ? (FLOAT)window.width / (FLOAT)window.height : 1.0f;
    FLOAT height = bounds ? fabsf(bounds->max.z - bounds->min.z) : 2.5f;
    FLOAT radius = MAX(1.0f, height * g_preview_scale * 0.6f);
    FLOAT near_clip = MAX(0.01f, g_orbit.distance * 0.01f);
    FLOAT far_clip = MAX(100.0f, g_orbit.distance + radius * 8.0f);
    char line[512];

    entity.model = model;
    entity.scale = g_preview_scale;
    entity.radius = radius;
    entity.frame = now;
    entity.oldframe = now;
    entity.flags = RF_GROUND_ANCHOR | RF_NO_SHADOW | RF_NO_FOGOFWAR;
#ifdef WOW
    entity.appearance = g_wow_appearance;
    entity.equipment = g_wow_equipment;
#endif

    Matrix4_identity(&viewdef.textureMatrix);
    Viewer_OrbitBuildCamera(&g_orbit, aspect, 35.0f, near_clip, far_clip, &viewdef.viewProjectionMatrix);
    Viewer_OrbitBuildLight(&g_orbit, &(VECTOR3){ 0.0f, 0.0f, 0.0f }, MAX(32.0f, radius * 2.0f), &viewdef.lightMatrix);
    viewdef.viewport = (RECT){ 0, 0, 1, 1 };
    viewdef.scissor = (RECT){ 0, 0, 1, 1 };
    viewdef.time = now;
    viewdef.deltaTime = 16;
    viewdef.lerpfrac = 0.0f;
    viewdef.num_entities = 1;
    viewdef.entities = &entity;
    viewdef.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG | RDF_NOFOGMASK;

    re->BeginFrame();
    re->RenderFrame(&viewdef);
    Tool_DrawString(re, "m2tool: WoW M2 viewer", 10, 10);
    Tool_DrawString(re, g_model_path, 10, 28);
    snprintf(line,
             sizeof(line),
             "appearance=%u equipment=%u scale=%.3f",
             (unsigned)g_wow_appearance,
             (unsigned)g_wow_equipment,
             g_preview_scale);
    Tool_DrawString(re, line, 10, 46);
    re->EndFrame();
}

static int RunViewer(void) {
    refExport_t re;
    LPMODEL model;
    BOX3 bounds = { 0 };
    BOOL has_bounds;
    FLOAT extent = 2.5f;
    FLOAT height;
    FLOAT orbit_distance;
    BOOL running = true;

    re = R_GetAPI((refImport_t){
        .FS_ReadFile = Tool_FS_ReadFile,
        .FS_FreeFile = Tool_MemFree,
        .MemAlloc = Tool_MemAlloc,
        .MemFree = Tool_MemFree,
        .error = errorf,
    });

    has_bounds = LoadPreviewBounds(&bounds, &extent);
    height = has_bounds ? fabsf(bounds.max.z - bounds.min.z) : 2.5f;
    g_preview_scale = extent > 0.0001f ? MAX(1.0f, 24.0f / extent) : 1.0f;
    orbit_distance = MAX(40.0f, extent * g_preview_scale * 2.4f);
    Viewer_OrbitInit(&g_orbit,
                     (VECTOR3){ 0.0f, 0.0f, height * g_preview_scale * 0.55f },
                     orbit_distance,
                     -45.0f,
                     18.0f);
    g_orbit.reverse_drag = true;

    fprintf(stderr, "m2tool: renderer init\n");
    re.Init(VIEWER_WINDOW_WIDTH, VIEWER_WINDOW_HEIGHT);
    fprintf(stderr, "m2tool: loading model %s\n", g_model_path);
    model = re.LoadModel(g_model_path);
    if (!model || model->modeltype != ID_MD20) {
        fprintf(stderr, "m2tool: failed to load M2 model %s\n", g_model_path);
        re.Shutdown();
        return 1;
    }
    fprintf(stderr,
            "m2tool: viewer ready scale=%.3f orbit_distance=%.3f%s\n",
            g_preview_scale,
            orbit_distance,
            has_bounds ? "" : " bounds=unavailable");

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            } else {
                Viewer_OrbitHandleEvent(&g_orbit, &event);
            }
        }

        RenderViewerFrame(&re, model, SDL_GetTicks(), has_bounds ? &bounds : NULL);
        if (g_run_once) {
            running = false;
        }
    }

    re.ReleaseModel(model);
    re.Shutdown();
    return 0;
}

int main(int argc, char **argv) {
    int archive_count = 0;

    if (argc <= 1) {
        usage();
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-?") || !strcmp(argv[i], "--help")) {
            usage();
            return 0;
        } else if (!strcmp(argv[i], "-mpq") && i + 1 < argc) {
            if (archive_count >= (int)(sizeof(archives) / sizeof(archives[0]))) {
                errorf("m2tool: too many MPQ archives");
            }
            if (!Tool_AddArchive(archives, sizeof(archives) / sizeof(archives[0]), argv[++i])) {
                return 1;
            }
            archive_count++;
        } else if (!strcmp(argv[i], "-model") && i + 1 < argc) {
            g_model_path = argv[++i];
        } else if (!strcmp(argv[i], "--skin") && i + 1 < argc) {
            g_skin_path = argv[++i];
        } else if (!strcmp(argv[i], "--anim") && i + 1 < argc) {
            g_anim_check = argv[++i];
        } else if (!strcmp(argv[i], "--info")) {
            g_info_only = true;
        } else if (!strcmp(argv[i], "--dump-all")) {
            g_info_only = true;
            g_dump_all = true;
        } else if (!strcmp(argv[i], "--viewer")) {
            g_info_only = false;
        } else if (!strcmp(argv[i], "--once")) {
            g_run_once = true;
        } else if (!strcmp(argv[i], "--wow-player-config")) {
            g_wow_player_config = true;
        } else if (!strcmp(argv[i], "--wow-player-config-only")) {
            g_wow_player_config = true;
            g_wow_player_config_only = true;
            g_info_only = true;
        } else if (!strcmp(argv[i], "--appearance") && i + 1 < argc) {
            g_wow_player_config = true;
            g_wow_appearance = (DWORD)strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--equipment") && i + 1 < argc) {
            g_wow_player_config = true;
            g_wow_equipment = (DWORD)strtoul(argv[++i], NULL, 0);
        } else {
            usage();
            errorf("m2tool: unknown or incomplete argument: %s", argv[i]);
        }
    }

    if (!archive_count) {
        errorf("m2tool: at least one -mpq archive is required");
    }
    if (!g_model_path) {
        errorf("m2tool: -model is required");
    }

    Tool_SetSheetHost(archives, sizeof(archives) / sizeof(archives[0]));
    if (g_info_only || g_anim_check) {
        InspectModel();
    } else {
        int result = RunViewer();
        Tool_CloseArchives(archives, sizeof(archives) / sizeof(archives[0]));
        return result;
    }
    Tool_CloseArchives(archives, sizeof(archives) / sizeof(archives[0]));
    return 0;
}
