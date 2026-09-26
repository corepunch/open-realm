#include "tools/viewer_common.h"
#include "client/tr_public.h"
#include "games/world-of-warcraft/common/wow_character_utils.h"

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
    uint32_t magic;
    uint32_t version;
    m2Array_t name;
    uint32_t flags;
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
    vector3_t bounding_box_min;
    vector3_t bounding_box_max;
    float bounding_sphere_radius;
    vector3_t collision_box_min;
    vector3_t collision_box_max;
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
    uint16_t animation_id;
    uint16_t sub_animation_id;
    uint32_t start_timestamp;
    uint32_t end_timestamp;
    float movement_speed;
    uint32_t flags;
    int16_t probability;
    uint16_t padding;
    uint32_t minimum_repetitions;
    uint32_t maximum_repetitions;
    uint32_t blend_time;
    vector3_t min;
    vector3_t max;
    float radius;
    int16_t next_animation;
    uint16_t alias_next;
} m2SequenceClassic_t;

typedef struct {
    uint16_t animation_id;
    uint16_t sub_animation_id;
    uint32_t length;
    float movement_speed;
    uint32_t flags;
    int16_t probability;
    uint16_t padding;
    uint32_t minimum_repetitions;
    uint32_t maximum_repetitions;
    uint32_t blend_time;
    vector3_t min;
    vector3_t max;
    float radius;
    int16_t next_animation;
    uint16_t alias_next;
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
    uint32_t start;
    uint32_t end;
} m2Range_t;

typedef struct {
    uint32_t auCompQ[2];
} m2CompQuat_t;

typedef struct {
    uint32_t attachment_id;
    uint16_t bone_index;
    uint16_t padding;
    vector3_t position;
    m2Track_t visibility_track;
} m2AttachmentModern_t;

typedef struct {
    uint32_t attachment_id;
    uint16_t bone_index;
    uint16_t padding;
    vector3_t position;
    m2TrackClassic_t visibility_track;
} m2AttachmentClassic_t;

typedef struct {
    uint32_t bone_id;
    uint32_t flags;
    uint16_t parent_index;
    uint16_t dist_to_parent;
    uint32_t union_data;
    m2Track_t translation_track;
    m2Track_t rotation_track;
    m2Track_t scale_track;
    vector3_t pivot;
} m2CompBoneModern_t;

typedef struct {
    uint32_t bone_id;
    uint32_t flags;
    uint16_t parent_index;
    uint16_t submesh_id;
    m2TrackClassic_t translation_track;
    m2TrackClassic_t rotation_track;
    m2TrackClassic_t scale_track;
    vector3_t pivot;
} m2CompBoneClassic_t;

typedef struct {
    vector3_t pos;
    uint8_t bone_weights[4];
    uint8_t bone_indices[4];
    vector3_t normal;
    vector2_t tex_coords[2];
} m2Vertex_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    m2Array_t filename;
} m2Texture_t;

typedef struct {
    uint32_t magic;
    m2Array_t vertices;
    m2Array_t indices;
    m2Array_t bones;
    m2Array_t sections;
    m2Array_t batches;
    uint32_t bone_count_max;
} m2SkinHeader_t;

typedef struct {
    m2Array_t vertices;
    m2Array_t indices;
    m2Array_t bones;
    m2Array_t sections;
    m2Array_t batches;
    uint32_t bone_count_max;
} m2EmbeddedView_t;

typedef struct {
    uint16_t skin_section_id;
    uint16_t level;
    uint16_t vertex_start;
    uint16_t vertex_count;
    uint16_t index_start;
    uint16_t index_count;
    uint16_t bone_count;
    uint16_t bone_combo_index;
    uint16_t bone_influences;
    uint16_t center_bone_index;
    vector3_t center_position;
    vector3_t sort_center_position;
    float sort_radius;
} m2SkinSection_t;

typedef struct {
    uint16_t skin_section_id;
    uint16_t level;
    uint16_t vertex_start;
    uint16_t vertex_count;
    uint16_t index_start;
    uint16_t index_count;
    uint16_t bone_count;
    uint16_t bone_combo_index;
    uint16_t bone_influences;
    uint16_t center_bone_index;
    vector3_t center_position;
} m2SkinSectionLegacy_t;

typedef struct {
    uint8_t flags;
    signed char priority_plane;
    uint16_t shader_id;
    uint16_t skin_section_index;
    uint16_t geoset_index;
    int16_t color_index;
    uint16_t material_index;
    uint16_t material_layer;
    uint16_t texture_count;
    uint16_t texture_combo_index;
    uint16_t texture_coord_combo_index;
    uint16_t texture_weight_combo_index;
    uint16_t texture_transform_combo_index;
} m2Batch_t;

typedef struct {
    m2Array_t times;
    m2Array_t values;
} m2PartTrack_t;

/* Modern (WotLK+) particle emitter binary layout — m2Track_t (20 bytes each). */
typedef struct {
    uint32_t particle_id, flags; vector3_t position; uint16_t bone_index, texture_index;
    m2Array_t geometry_mdl, recursion_mdl;
    uint8_t blend_mode, emitter_type; uint16_t color_index, pad; int16_t priority_plane; uint16_t rows, cols;
    m2Track_t speed_track, variation_track, latitude_track, longitude_track, gravity_track, life_track;
    float life_variation;
    m2Track_t emission_rate_track;
    float emission_rate_variation;
    m2Track_t width_track, length_track, zsource_track;
    m2PartTrack_t color_track, alpha_track, scale_track;
    vector2_t scale_variation;
    m2PartTrack_t head_cell_track, tail_cell_track;
    float tail_length, twinkle_fps, twinkle_onoff, twinkle_scale[2];
    float ivel_scale, drag, initial_spin, initial_spin_variation, spin, spin_variation;
    vector3_t tumble_min, tumble_max;
    vector3_t wind_vector; float wind_time;
    float follow_speed1, follow_scale1, follow_speed2, follow_scale2;
    m2Array_t spline;
    m2Track_t visibility_track;
} m2ParticleModern_t;

/* Classic/TBC stores ten contiguous 28-byte tracks followed by static lifecycle values. */
typedef struct {
    uint32_t particle_id, flags; vector3_t position; uint16_t bone_index, texture_index;
    m2Array_t geometry_mdl, recursion_mdl;
    uint8_t blend_mode, emitter_type; uint16_t color_index, pad; int16_t priority_plane; uint16_t rows, cols;
    m2TrackClassic_t speed_track, variation_track, latitude_track, longitude_track, gravity_track, life_track;
    m2TrackClassic_t emission_rate_track, width_track, length_track, visibility_track;
    float midpoint;
    uint32_t colors[3];
    float scales[3];
    uint8_t tail[0x1f8 - 0x168];
} m2ParticleClassic_t;

_Static_assert(sizeof(m2ParticleClassic_t) == 0x1f8, "classic M2 particles are 0x1f8 bytes");

/* Modern ribbon emitter — m2Track_t (20 bytes each). */
typedef struct {
    uint32_t ribbon_id; uint16_t bone_index, pad0; vector3_t position;
    m2Array_t texture_indices, material_indices;
    m2Track_t color_track, alpha_track, height_above_track, height_below_track;
    float edges_per_second, edge_lifetime, gravity;
    uint16_t texture_rows, texture_cols;
    m2Track_t texture_slot_track, visibility_track;
    int16_t priority_plane; uint16_t pad1;
} m2RibbonModern_t;

/* Classic ribbon emitter — m2TrackClassic_t (24 bytes each). */
typedef struct {
    uint32_t ribbon_id; uint16_t bone_index, pad0; vector3_t position;
    m2Array_t texture_indices, material_indices;
    m2TrackClassic_t color_track, alpha_track, height_above_track, height_below_track;
    float edges_per_second, edge_lifetime, gravity;
    uint16_t texture_rows, texture_cols;
    m2TrackClassic_t texture_slot_track, visibility_track;
    int16_t priority_plane; uint16_t pad1;
} m2RibbonClassic_t;

static handle_t archives[64] = { 0 };
static cstring_t g_model_path = NULL;
static cstring_t g_skin_path = NULL;
static cstring_t g_anim_check = NULL;
static bool g_info_only = false;
static bool g_dump_all = false;
static bool g_wow_player_config = false;
static bool g_wow_player_config_only = false;
static bool g_run_once = false;
static uint32_t g_wow_appearance = (1u << 23); /* Wow_PackAppearance(..., WOW_CLASS_WARRIOR, ...) */
static uint32_t g_wow_equipment = 0;
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

static void errorf(cstring_t fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static void Tool_DrawString(refExport_t const *re, cstring_t string, int x, int y) {
    if (!re || !string) {
        return;
    }
    for (uint32_t i = 0; string[i]; i++) {
        re->DrawChar(x + (int)i * 8, y, (uint8_t)string[i]);
    }
}

static bool PathHasExtension(cstring_t path, cstring_t extension) {
    size_t path_len;
    size_t ext_len;

    if (!path || !extension) {
        return false;
    }
    path_len = strlen(path);
    ext_len = strlen(extension);
    return path_len >= ext_len && strcasecmp(path + path_len - ext_len, extension) == 0;
}

static bool CopyWithExtension(cstring_t path, cstring_t extension, string_t out, uint32_t out_size) {
    cstring_t dot;
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

static handle_t OpenFileFallback(cstring_t path, string_t resolved, uint32_t resolved_size) {
    handle_t file;
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

static uint8_t * ReadWholeFile(cstring_t path, uint32_t * out_size, string_t resolved, uint32_t resolved_size) {
    handle_t file = OpenFileFallback(path, resolved, resolved_size);
    uint32_t size;
    uint32_t read_size = 0;
    uint8_t * data;

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
    M2TOOL_SLOT_NONE,
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
    uint8_t * data;
    uint32_t size;
    uint32_t records;
    uint32_t fields;
    uint32_t record_size;
    uint32_t string_size;
    uint8_t const *records_base;
    uint8_t const *strings_base;
} m2ToolDbc_t;

typedef struct {
    cstring_t texture[M2TOOL_CHAR_TEX_COUNT][7];
    uint32_t geoset[M2TOOL_NUM_GEOSET_GROUPS];
    uint32_t flags;
    uint32_t display_ids[16];
    uint32_t display_count;
} m2ToolWowOutfit_t;

static uint32_t Read32LE(uint8_t const *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool LoadDbc(cstring_t filename, m2ToolDbc_t *dbc) {
    PATHSTR resolved = { 0 };
    uint32_t size = 0;

    if (!filename || !dbc) {
        return false;
    }
    memset(dbc, 0, sizeof(*dbc));
    dbc->data = ReadWholeFile(filename, &size, resolved, sizeof(resolved));
    if (!dbc->data || size <= 20 || *(uint32_t const *)dbc->data != ID_WDBC) {
        SAFE_DELETE(dbc->data, Tool_MemFree);
        return false;
    }
    dbc->size = size;
    dbc->records = Read32LE(dbc->data + 4);
    dbc->fields = Read32LE(dbc->data + 8);
    dbc->record_size = Read32LE(dbc->data + 12);
    dbc->string_size = Read32LE(dbc->data + 16);
    if (!dbc->fields || dbc->record_size < sizeof(uint32_t) ||
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

static uint32_t DbcField(m2ToolDbc_t const *dbc, uint8_t const *record, uint32_t field) {
    if (!dbc || !record || field >= dbc->fields || field * sizeof(uint32_t) + sizeof(uint32_t) > dbc->record_size) {
        return 0;
    }
    return Read32LE(record + field * sizeof(uint32_t));
}

static cstring_t DbcString(m2ToolDbc_t const *dbc, uint32_t offset) {
    if (!dbc || !dbc->data || offset == 0 || offset >= dbc->string_size) {
        return NULL;
    }
    return (cstring_t)(dbc->strings_base + offset);
}

static uint8_t const *DbcFindID(m2ToolDbc_t const *dbc, uint32_t wanted_id) {
    if (!dbc || !dbc->data) {
        return NULL;
    }
    FOR_LOOP(i, dbc->records) {
        uint8_t const *record = dbc->records_base + i * dbc->record_size;
        if (DbcField(dbc, record, 0) == wanted_id) {
            return record;
        }
    }
    return NULL;
}

static bool CharacterRaceGender(cstring_t model_path, uint32_t *race_id, uint32_t *gender_id) {
    cstring_t character;
    cstring_t race;
    cstring_t gender;
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

static uint8_t WowClassFromAppearance(uint32_t appearance) {
    uint8_t class_id = (uint8_t)((appearance >> 23) & 0x0f);
    return class_id ? class_id : 1;
}

static uint32_t ItemDisplayInfoTextureBase(m2ToolDbc_t const *dbc) {
    if (!dbc) {
        return 0;
    }
    if (dbc->fields >= 25) {
        return 15;
    }
    return dbc->fields >= 22 ? 14 : 0;
}

static uint32_t ItemDisplayInfoGeosetBase(m2ToolDbc_t const *dbc) {
    return dbc && dbc->fields >= 22 ? 7 : 0;
}

static uint32_t ItemDisplayInfoFlagsField(m2ToolDbc_t const *dbc) {
    return dbc && dbc->fields >= 22 ? 10 : 0;
}

static void AddDisplayInfoToOutfit(m2ToolWowOutfit_t *outfit,
                                   m2ToolDbc_t const *item_display_info,
                                   uint32_t display_id,
                                   uint32_t slot) {
    static uint32_t const slot_geoset_group_map[M2TOOL_SLOT_COUNT][3] = {
        /* NONE */      { 0, 0, 0 },
        /* HEAD */      { 0, 0, 0 },
        /* SHOULDERS */ { 0, 0, 0 },
        /* CHEST */     { 8, 0, 13 },
        /* SHIRT */     { 8, 0, 0 },
        /* BELT */      { 0, 0, 0 },
        /* LEGS */      { 13, 0, 0 },
        /* BOOTS */     { 5, 0, 0 },
        /* GLOVES */    { 4, 0, 0 },
        /* TABARD */    { 0, 0, 0 },
        /* CAPE */      { 15, 0, 0 },
    };
    uint8_t const *record;
    uint32_t texture_base;
    uint32_t geoset_base;
    uint32_t flags_field;

    if (!outfit || !item_display_info || display_id == 0 || display_id == 0xffffffffu) {
        return;
    }
    record = DbcFindID(item_display_info, display_id);
    if (!record) {
        return;
    }
    if (slot == M2TOOL_SLOT_NONE || slot >= M2TOOL_SLOT_COUNT) return;
    if (outfit->display_count < sizeof(outfit->display_ids) / sizeof(outfit->display_ids[0])) {
        outfit->display_ids[outfit->display_count++] = display_id;
    }

    texture_base = ItemDisplayInfoTextureBase(item_display_info);
    geoset_base = ItemDisplayInfoGeosetBase(item_display_info);
    flags_field = ItemDisplayInfoFlagsField(item_display_info);
    FOR_LOOP(i, 3) {
        uint32_t geoset_group = DbcField(item_display_info, record, geoset_base + i);
        if (geoset_group && slot < M2TOOL_SLOT_COUNT) {
            uint32_t group = slot_geoset_group_map[slot][i];
            if (group && group < M2TOOL_NUM_GEOSET_GROUPS) {
                outfit->geoset[group] = geoset_group;
            }
        }
    }
    outfit->flags |= DbcField(item_display_info, record, flags_field);
    /* A worn tabard activates the hanging tabard mesh (geoset group 12). */
    if (slot == M2TOOL_SLOT_TABARD) outfit->geoset[12] = 2;
    FOR_LOOP(i, M2TOOL_CHAR_TEX_COUNT) {
        cstring_t texture = DbcString(item_display_info, DbcField(item_display_info, record, texture_base + i));
        signed char priority = Wow_CharacterTexturePriority(slot, i);
        if (texture && *texture && priority >= 0) outfit->texture[i][priority] = texture;
    }
}

static void AddDisplayInfoListToOutfit(m2ToolWowOutfit_t *outfit,
                                       m2ToolDbc_t const *item_display_info,
                                       uint32_t const *display_ids,
                                       uint32_t display_count,
                                       uint32_t slot) {
    FOR_LOOP(i, display_count) {
        AddDisplayInfoToOutfit(outfit, item_display_info, display_ids[i], slot);
    }
}

typedef struct {
    uint32_t display_ids[4];
} m2ToolEquipmentItem_t;

typedef struct {
    uint32_t race_id;
    uint32_t gender_id;
    m2ToolEquipmentItem_t items[256];
} m2ToolEquipmentSlotItems_t;

static m2ToolEquipmentItem_t const *EquipmentSlotItem(m2ToolEquipmentSlotItems_t const *lists,
                                                      uint32_t list_count,
                                                      uint32_t race_id,
                                                      uint32_t gender_id,
                                                      uint8_t item_index) {
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
                                     uint32_t list_count,
                                     uint32_t race_id,
                                     uint32_t gender_id,
                                     uint8_t item_index,
                                     uint32_t slot) {
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
                                uint32_t race_id,
                                uint32_t gender_id,
                                uint32_t equipment) {
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

static bool LoadWowStartOutfit(cstring_t model_path,
                               uint32_t appearance,
                               uint32_t equipment,
                               m2ToolWowOutfit_t *outfit) {
    m2ToolDbc_t start = { 0 };
    m2ToolDbc_t item = { 0 };
    uint32_t race_id;
    uint32_t gender_id;
    uint32_t class_id;
    bool found = false;

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
        uint8_t const *record = start.records_base + i * start.record_size;
        uint32_t race_class_gender = DbcField(&start, record, 1);
        uint32_t record_race = race_class_gender & 0xff;
        uint32_t record_class = (race_class_gender >> 8) & 0xff;
        uint32_t record_gender = (race_class_gender >> 16) & 0xff;

        if (record_race != race_id || record_class != class_id || record_gender != gender_id) {
            continue;
        }
        FOR_LOOP(display, 12) {
            AddDisplayInfoToOutfit(outfit, &item, DbcField(&start, record, 14 + display),
                                   Wow_CharacterSlotForInventoryType(DbcField(&start, record, 26 + display)));
        }
        ApplyEquipmentItems(outfit, &item, race_id, gender_id, equipment);
        found = true;
        break;
    }

    FreeDbc(&start);
    FreeDbc(&item);
    return found;
}

static bool ComponentTexturePath(cstring_t stem, uint8_t slot, cstring_t model_path, string_t out, uint32_t out_size) {
    static cstring_t const folders[M2TOOL_CHAR_TEX_COUNT] = {
        "ArmUpperTexture",
        "ArmLowerTexture",
        "HandTexture",
        "TorsoUpperTexture",
        "TorsoLowerTexture",
        "LegUpperTexture",
        "LegLowerTexture",
        "FootTexture"
    };
    uint32_t race_id;
    uint32_t gender_id;
    cstring_t gender_suffix;
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

static bool WowVisibleSection(uint16_t section_id, m2ToolWowOutfit_t const *outfit,
                              uint16_t const *available, uint32_t available_count) {
    uint32_t group, geoset, expected;

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
        case 5:  expected = Wow_CharacterGeosetPick(available, available_count, 5, 501 + geoset, 501); break;
        case 7:  expected = geoset ? 700 + geoset : 702; break;
        case 8:  expected = 801 + geoset; break;
        case 9:  expected = 901 + geoset; break;
        case 10: return false;
        case 11: return false;
        case 12: expected = 1200 + geoset; break;
        case 13:
            if (outfit->flags & 0x4) {
                return false;
            }
            expected = Wow_CharacterGeosetPick(available, available_count, 13, 1301 + geoset, 1301);
            break;
        case 15: expected = 1501; break;
        default: return false;
    }
    return section_id == expected;
}

static bool TagEquals(uint8_t const *tag, uint32_t fourcc) {
    uint32_t value;
    memcpy(&value, tag, sizeof(value));
    return value == fourcc;
}

static bool FindM2Payload(uint8_t const *data, uint32_t size, uint8_t const **payload, uint32_t *payload_size) {
    uint32_t magic;
    uint32_t offset;

    if (!data || size < sizeof(uint32_t) || !payload || !payload_size) {
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
        uint8_t const *tag = data + offset;
        uint32_t chunk_size;
        memcpy(&chunk_size, data + offset + 4, sizeof(chunk_size));
        offset += 8;
        if (chunk_size > size - offset) {
            return false;
        }
        if (TagEquals(tag, ID_MD20) ||
            TagEquals(tag, ID_MD21) ||
            TagEquals(tag, ID_12DM)) {
            if (chunk_size >= sizeof(uint32_t)) {
                uint32_t inner_magic;
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

static uint32_t ReadU32(uint8_t const *p) {
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static float ReadFloat(uint8_t const *p) {
    float value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static m2Array_t ReadArray(uint8_t const *p) {
    m2Array_t array;
    memcpy(&array, p, sizeof(array));
    return array;
}

static vector3_t ReadVec3(uint8_t const *p) {
    return (vector3_t){ ReadFloat(p), ReadFloat(p + 4), ReadFloat(p + 8) };
}

static void SkipArray(uint32_t *offset) {
    *offset += sizeof(m2Array_t);
}

static m2Array_t NextArray(uint8_t const *data, uint32_t *offset) {
    m2Array_t value = ReadArray(data + *offset);
    *offset += sizeof(m2Array_t);
    return value;
}

static bool ParseHeader(uint8_t const *data, uint32_t size, m2HeaderInfo_t *out) {
    uint32_t offset;
    bool legacy;

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
    offset += sizeof(uint32_t);
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
        offset += sizeof(uint32_t);
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

static bool ArrayRange(m2Array_t array, uint32_t elem_size, uint32_t file_size, uint32_t *offset, uint32_t *bytes) {
    if (array.count <= 0 || array.offset < 0 || elem_size == 0) {
        return false;
    }
    if ((uint32_t)array.count > (((uint32_t)~0u) / elem_size)) {
        return false;
    }
    *offset = (uint32_t)array.offset;
    *bytes = (uint32_t)array.count * elem_size;
    return *offset <= file_size && *bytes <= file_size - *offset;
}

static void const *ArrayPtr(uint8_t const *base, uint32_t file_size, m2Array_t array, uint32_t elem_size) {
    uint32_t offset;
    uint32_t bytes;
    if (!ArrayRange(array, elem_size, file_size, &offset, &bytes)) {
        return NULL;
    }
    return base + offset;
}

static cstring_t StringPtr(uint8_t const *base, uint32_t file_size, m2Array_t array) {
    uint32_t offset;
    uint32_t bytes;
    if (!ArrayRange(array, 1, file_size, &offset, &bytes) || bytes == 0) {
        return NULL;
    }
    if (!memchr(base + offset, '\0', bytes)) {
        return NULL;
    }
    return (cstring_t)(base + offset);
}

static void PrintVec3(cstring_t label, vector3_t v) {
    printf("  %-22s %.6f %.6f %.6f\n", label, v.x, v.y, v.z);
}

static void PrintBoundsMetrics(cstring_t label, vector3_t min, vector3_t max) {
    vector3_t extent = { max.x - min.x, max.y - min.y, max.z - min.z };
    vector3_t center = {
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

static void PrintArray(cstring_t label, m2Array_t array, bool count_only) {
    if (count_only) {
        printf("  %-28s count=%d\n", label, array.count);
    } else {
        printf("  %-28s count=%d offset=%d\n", label, array.count, array.offset);
    }
}

static void PrintAttachments(uint8_t const *data, uint32_t size, m2HeaderInfo_t const *header) {
    uint8_t const *attachments;
    uint16_t const *lookup;
    bool legacy = header->version <= 263;
    uint32_t stride = legacy ? sizeof(m2AttachmentClassic_t) : sizeof(m2AttachmentModern_t);

    attachments = ArrayPtr(data, size, header->attachments, stride);
    if (!attachments || header->attachments.count <= 0) {
        return;
    }

    printf("attachments:\n");
    FOR_LOOP(i, (uint32_t)header->attachments.count) {
        uint32_t attachment_id;
        uint16_t bone_index;
        vector3_t position;

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
    FOR_LOOP(i, (uint32_t)header->attachment_lookup.count) {
        printf("  [%03u] -> %u\n", (unsigned)i, (unsigned)lookup[i]);
    }
}

static void PrintEvents(uint8_t const *data, uint32_t size, m2HeaderInfo_t const *header) {
    /* Both modern and legacy event structs are 44 bytes:
     *   4(id) + 4(data) + 2(bone) + 2(pad) + 12(pos) + 20(track) = 44
     * Track is M2TrackBase: 2+2 header + two M2Arrays (16 bytes). */
    uint8_t const *events;

    if (header->events.count <= 0) {
        return;
    }
    events = ArrayPtr(data, size, header->events, 44);
    if (!events) {
        return;
    }

    printf("events:\n");
    FOR_LOOP(i, (uint32_t)header->events.count) {
        uint8_t const *ev = events + i * 44;
        char id_str[5];
        uint32_t event_data;
        uint16_t bone;
        memcpy(id_str, ev + 0, 4);
        id_str[4] = '\0';
        memcpy(&event_data, ev + 4, sizeof(uint32_t));
        memcpy(&bone, ev + 8, sizeof(uint16_t));
        printf("  [%03u] id=%.4s data=%u bone=%u\n",
               (unsigned)i, id_str,
               (unsigned)event_data, (unsigned)bone);
    }
}

static void UpdateBounds(box3_t *bounds, vector3_t p, bool *has_bounds) {
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

static bool CalculateVertexBounds(uint8_t const *data, uint32_t size, m2Array_t vertices, box3_t *bounds) {
    m2Vertex_t const *items = ArrayPtr(data, size, vertices, sizeof(*items));
    bool has_bounds = false;

    if (!items || !bounds) {
        return false;
    }
    FOR_LOOP(i, (uint32_t)vertices.count) {
        UpdateBounds(bounds, items[i].pos, &has_bounds);
    }
    return has_bounds;
}

static cstring_t AnimationName(uint16_t id) {
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

static void PrintAnimations(uint8_t const *data, uint32_t size, m2HeaderInfo_t const *header) {
    uint32_t offset;
    uint32_t bytes;

    if (!ArrayRange(header->animations,
                    header->version <= 263 ? sizeof(m2SequenceClassic_t) : sizeof(m2SequenceModern_t),
                    size,
                    &offset,
                    &bytes)) {
        printf("animations: unavailable\n");
        return;
    }
    printf("animations:\n");
    FOR_LOOP(i, (uint32_t)header->animations.count) {
        if (header->version <= 263) {
            m2SequenceClassic_t const *seq = (m2SequenceClassic_t const *)(data + offset + i * sizeof(*seq));
            cstring_t name = AnimationName(seq->animation_id);
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
            cstring_t name = AnimationName(seq->animation_id);
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

static uint32_t SequenceStart(uint8_t const *sequence, bool classic) {
    return classic ? ((m2SequenceClassic_t const *)sequence)->start_timestamp : 0;
}

static uint32_t SequenceDuration(uint8_t const *sequence, bool classic) {
    if (classic) {
        m2SequenceClassic_t const *seq = (m2SequenceClassic_t const *)sequence;
        return seq->end_timestamp > seq->start_timestamp ? seq->end_timestamp - seq->start_timestamp : 0;
    }
    return ((m2SequenceModern_t const *)sequence)->length;
}

static uint16_t SequenceAnimationId(uint8_t const *sequence, bool classic) {
    return classic
        ? ((m2SequenceClassic_t const *)sequence)->animation_id
        : ((m2SequenceModern_t const *)sequence)->animation_id;
}

static bool ParseSequenceSelector(cstring_t selector, uint32_t max_count, uint32_t *out_index) {
    char *end = NULL;
    unsigned long value;

    if (!selector || !*selector || !out_index) {
        return false;
    }
    value = strtoul(selector, &end, 10);
    if (end && *end == '\0' && value < max_count) {
        *out_index = (uint32_t)value;
        return true;
    }
    return false;
}

static bool FindSequenceByName(uint8_t const *sequences,
                               uint32_t sequence_count,
                               uint32_t sequence_stride,
                               bool classic,
                               cstring_t selector,
                               uint32_t *out_index) {
    if (ParseSequenceSelector(selector, sequence_count, out_index)) {
        return true;
    }
    FOR_LOOP(i, sequence_count) {
        uint8_t const *sequence = sequences + i * sequence_stride;
        cstring_t name = AnimationName(SequenceAnimationId(sequence, classic));
        if (name && !strcasecmp(name, selector)) {
            *out_index = i;
            return true;
        }
    }
    return false;
}

static uint32_t ClassicTrackKeyRange(uint8_t const *data,
                                  uint32_t size,
                                  m2TrackClassic_t const *track,
                                  uint32_t sequence_index,
                                  uint32_t elem_size,
                                  m2Range_t *out_range,
                                  uint32_t const **out_times,
                                  uint8_t const **out_keys) {
    m2Range_t const *ranges;
    m2Range_t range;
    uint32_t const *times;
    uint8_t const *keys;
    uint32_t count;

    if (!track || !out_range || !out_times || !out_keys) {
        return 0;
    }
    ranges = ArrayPtr(data, size, track->ranges, sizeof(*ranges));
    if (!ranges || track->ranges.count <= 0) {
        return 0;
    }
    if (sequence_index >= (uint32_t)track->ranges.count) {
        sequence_index = 0;
    }
    range = ranges[sequence_index];
    if (range.end < range.start) {
        return 0;
    }
    times = ArrayPtr(data, size, track->times, sizeof(*times));
    keys = ArrayPtr(data, size, track->keys, elem_size);
    if (!times || !keys ||
        range.start >= (uint32_t)track->times.count ||
        range.start >= (uint32_t)track->keys.count) {
        return 0;
    }
    count = range.end - range.start + 1;
    count = MIN(count, (uint32_t)track->times.count - range.start);
    count = MIN(count, (uint32_t)track->keys.count - range.start);
    if (count == 0) {
        return 0;
    }
    *out_range = range;
    *out_times = times + range.start;
    *out_keys = keys + range.start * elem_size;
    return count;
}

static uint32_t ModernTrackKeyRange(uint8_t const *data,
                                 uint32_t size,
                                 m2Track_t const *track,
                                 uint32_t sequence_index,
                                 uint32_t elem_size,
                                 uint32_t const **out_times,
                                 uint8_t const **out_keys) {
    m2SequenceTimes_t const *sequence_times;
    m2SequenceKeys_t const *sequence_keys;
    uint32_t const *times;
    uint8_t const *keys;

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
    if (sequence_index >= (uint32_t)track->sequence_times.count ||
        sequence_index >= (uint32_t)track->sequence_keys.count) {
        sequence_index = 0;
    }
    times = ArrayPtr(data, size, sequence_times[sequence_index].times, sizeof(*times));
    keys = ArrayPtr(data, size, sequence_keys[sequence_index].keys, elem_size);
    if (!times || !keys) {
        return 0;
    }
    *out_times = times;
    *out_keys = keys;
    return MIN((uint32_t)sequence_times[sequence_index].times.count,
               (uint32_t)sequence_keys[sequence_index].keys.count);
}

static vector3_t KeyVec3(uint8_t const *keys, uint32_t index) {
    return *(vector3_t const *)(keys + index * sizeof(vector3_t));
}

static void PrintTrackLine(cstring_t label,
                           uint32_t bone_index,
                           uint32_t flags,
                           uint16_t parent_index,
                           vector3_t pivot,
                           uint32_t count,
                           m2Range_t range,
                           uint32_t const *times,
                           uint8_t const *keys,
                           uint32_t elem_size,
                           bool classic,
                           bool vector_keys) {
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
            vector3_t first = KeyVec3(keys, 0);
            vector3_t mid = KeyVec3(keys, count / 2);
            vector3_t last = KeyVec3(keys, count - 1);
            printf(" first=(%.3f %.3f %.3f) mid=(%.3f %.3f %.3f) last=(%.3f %.3f %.3f)",
                   first.x, first.y, first.z,
                   mid.x, mid.y, mid.z,
                   last.x, last.y, last.z);
        } else if (elem_size == sizeof(quaternion_t)) {
            quaternion_t const *first = (quaternion_t const *)keys;
            quaternion_t const *mid = (quaternion_t const *)(keys + (count / 2) * elem_size);
            quaternion_t const *last = (quaternion_t const *)(keys + (count - 1) * elem_size);
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

static void PrintAnimationDiagnostics(uint8_t const *data, uint32_t size, m2HeaderInfo_t const *header, cstring_t selector) {
    uint32_t sequence_offset;
    uint32_t sequence_bytes;
    uint8_t const *sequences;
    uint8_t const *sequence;
    uint32_t sequence_stride;
    uint32_t sequence_index;
    bool classic = header->version <= 263;
    uint32_t bone_stride = classic ? sizeof(m2CompBoneClassic_t) : sizeof(m2CompBoneModern_t);
    uint8_t const *bones;
    uint32_t bones_offset;
    uint32_t bones_bytes;
    uint32_t sequence_count;
    uint32_t trans_bones = 0;
    uint32_t rot_bones = 0;
    uint32_t scale_bones = 0;
    uint32_t printed = 0;

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

    FOR_LOOP(i, (uint32_t)header->bones.count) {
        uint32_t const *times = NULL;
        uint8_t const *keys = NULL;
        uint32_t trans_count = 0;
        uint32_t rot_count = 0;
        uint32_t scale_count = 0;
        m2Range_t trans_range = { 0, 0 };
        m2Range_t rot_range = { 0, 0 };
        m2Range_t scale_range = { 0, 0 };
        uint32_t flags;
        uint16_t parent_index;
        vector3_t pivot;

        if (classic) {
            m2CompBoneClassic_t const *bone = (m2CompBoneClassic_t const *)(bones + i * bone_stride);
            flags = bone->flags;
            parent_index = bone->parent_index;
            pivot = bone->pivot;
            trans_count = ClassicTrackKeyRange(data, size, &bone->translation_track, sequence_index, sizeof(vector3_t), &trans_range, &times, &keys);
            if (trans_count) {
                trans_bones++;
            }
            times = NULL;
            keys = NULL;
            rot_count = ClassicTrackKeyRange(data, size, &bone->rotation_track, sequence_index, sizeof(quaternion_t), &rot_range, &times, &keys);
            if (rot_count) {
                rot_bones++;
            }
            times = NULL;
            keys = NULL;
            scale_count = ClassicTrackKeyRange(data, size, &bone->scale_track, sequence_index, sizeof(vector3_t), &scale_range, &times, &keys);
            if (scale_count) {
                scale_bones++;
            }
        } else {
            m2CompBoneModern_t const *bone = (m2CompBoneModern_t const *)(bones + i * bone_stride);
            flags = bone->flags;
            parent_index = bone->parent_index;
            pivot = bone->pivot;
            trans_count = ModernTrackKeyRange(data, size, &bone->translation_track, sequence_index, sizeof(vector3_t), &times, &keys);
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
            scale_count = ModernTrackKeyRange(data, size, &bone->scale_track, sequence_index, sizeof(vector3_t), &times, &keys);
            if (scale_count) {
                scale_bones++;
            }
        }

        if (printed < 16 && (trans_count || rot_count || scale_count || i == 0 || i == 22)) {
            if (classic) {
                m2CompBoneClassic_t const *bone = (m2CompBoneClassic_t const *)(bones + i * bone_stride);
                if (trans_count) {
                    trans_count = ClassicTrackKeyRange(data, size, &bone->translation_track, sequence_index, sizeof(vector3_t), &trans_range, &times, &keys);
                    PrintTrackLine("trans", i, flags, parent_index, pivot, trans_count, trans_range, times, keys, sizeof(vector3_t), true, true);
                }
                if (rot_count) {
                    rot_count = ClassicTrackKeyRange(data, size, &bone->rotation_track, sequence_index, sizeof(quaternion_t), &rot_range, &times, &keys);
                    PrintTrackLine("rot", i, flags, parent_index, pivot, rot_count, rot_range, times, keys, sizeof(quaternion_t), true, false);
                }
                if (scale_count) {
                    scale_count = ClassicTrackKeyRange(data, size, &bone->scale_track, sequence_index, sizeof(vector3_t), &scale_range, &times, &keys);
                    PrintTrackLine("scale", i, flags, parent_index, pivot, scale_count, scale_range, times, keys, sizeof(vector3_t), true, true);
                }
            } else {
                m2CompBoneModern_t const *bone = (m2CompBoneModern_t const *)(bones + i * bone_stride);
                if (trans_count) {
                    trans_count = ModernTrackKeyRange(data, size, &bone->translation_track, sequence_index, sizeof(vector3_t), &times, &keys);
                    PrintTrackLine("trans", i, flags, parent_index, pivot, trans_count, trans_range, times, keys, sizeof(vector3_t), false, true);
                }
                if (rot_count) {
                    rot_count = ModernTrackKeyRange(data, size, &bone->rotation_track, sequence_index, sizeof(m2CompQuat_t), &times, &keys);
                    PrintTrackLine("rot", i, flags, parent_index, pivot, rot_count, rot_range, times, keys, sizeof(m2CompQuat_t), false, false);
                }
                if (scale_count) {
                    scale_count = ModernTrackKeyRange(data, size, &bone->scale_track, sequence_index, sizeof(vector3_t), &times, &keys);
                    PrintTrackLine("scale", i, flags, parent_index, pivot, scale_count, scale_range, times, keys, sizeof(vector3_t), false, true);
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

static cstring_t TextureTypeName(uint32_t type) {
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

static void PrintTrackInfo(cstring_t label, bool classic, uint16_t type, uint32_t keys_off, uint32_t keys_n) {
    static cstring_t const track_names[] = { "none", "linear", "hermite", "bezier" };
    cstring_t tname = type < 4 ? track_names[type] : "?";
    printf("    %-22s type=%s(%u) keys_off=%u keys_n=%u\n", label, tname, (unsigned)type,
           (unsigned)keys_off, (unsigned)keys_n);
}

static void PrintParticleEmitters(uint8_t const *data, uint32_t size, m2HeaderInfo_t const *header) {
    bool classic = header->version <= 263;
    uint32_t stride = classic ? sizeof(m2ParticleClassic_t) : sizeof(m2ParticleModern_t);
    uint32_t count = (uint32_t)header->particle_emitters.count;
    uint32_t off = (uint32_t)header->particle_emitters.offset;
    if (!count) return;
    if (off + count * stride > size) {
        printf("particle_emitters: %u entries but data out of bounds (off=%u stride=%u size=%u)\n",
               (unsigned)count, (unsigned)off, (unsigned)stride, (unsigned)size);
        return;
    }
    printf("particle_emitters: %u (%s)\n", (unsigned)count, classic ? "classic/tbc" : "modern");
    FOR_LOOP(i, count) {
        uint8_t const *raw = data + off + i * stride;
        if (classic) {
            m2ParticleClassic_t const *p = (m2ParticleClassic_t const *)raw;
            printf("  [%u] id=0x%x flags=0x%x bone=%u tex=%u blend=%u etype=%u rows=%u cols=%u\n",
                   (unsigned)i, (unsigned)p->particle_id, (unsigned)p->flags,
                   (unsigned)p->bone_index, (unsigned)p->texture_index,
                   (unsigned)p->blend_mode, (unsigned)p->emitter_type,
                   (unsigned)p->rows, (unsigned)p->cols);
            PrintTrackInfo("speed_track",         classic, p->speed_track.track_type,         p->speed_track.keys.offset,         p->speed_track.keys.count);
            PrintTrackInfo("variation_track",      classic, p->variation_track.track_type,      p->variation_track.keys.offset,      p->variation_track.keys.count);
            PrintTrackInfo("latitude_track",       classic, p->latitude_track.track_type,       p->latitude_track.keys.offset,       p->latitude_track.keys.count);
            PrintTrackInfo("longitude_track",      classic, p->longitude_track.track_type,      p->longitude_track.keys.offset,      p->longitude_track.keys.count);
            PrintTrackInfo("gravity_track",        classic, p->gravity_track.track_type,        p->gravity_track.keys.offset,        p->gravity_track.keys.count);
            PrintTrackInfo("life_track",           classic, p->life_track.track_type,           p->life_track.keys.offset,           p->life_track.keys.count);
            PrintTrackInfo("emission_rate_track",  classic, p->emission_rate_track.track_type,  p->emission_rate_track.keys.offset,  p->emission_rate_track.keys.count);
            PrintTrackInfo("width_track",          classic, p->width_track.track_type,          p->width_track.keys.offset,          p->width_track.keys.count);
            PrintTrackInfo("length_track",         classic, p->length_track.track_type,         p->length_track.keys.offset,         p->length_track.keys.count);
            PrintTrackInfo("visibility_track",     classic, p->visibility_track.track_type,     p->visibility_track.keys.offset,     p->visibility_track.keys.count);
            printf("    lifecycle              midpoint=%.6f colors=%08x,%08x,%08x scales=%.6f,%.6f,%.6f\n",
                   p->midpoint, (unsigned)p->colors[0], (unsigned)p->colors[1], (unsigned)p->colors[2],
                   p->scales[0], p->scales[1], p->scales[2]);
        } else {
            m2ParticleModern_t const *p = (m2ParticleModern_t const *)raw;
            printf("  [%u] id=0x%x flags=0x%x bone=%u tex=%u blend=%u etype=%u rows=%u cols=%u\n",
                   (unsigned)i, (unsigned)p->particle_id, (unsigned)p->flags,
                   (unsigned)p->bone_index, (unsigned)p->texture_index,
                   (unsigned)p->blend_mode, (unsigned)p->emitter_type,
                   (unsigned)p->rows, (unsigned)p->cols);
            PrintTrackInfo("speed_track",         classic, p->speed_track.track_type,         p->speed_track.sequence_keys.offset, p->speed_track.sequence_keys.count);
            PrintTrackInfo("variation_track",      classic, p->variation_track.track_type,      p->variation_track.sequence_keys.offset, p->variation_track.sequence_keys.count);
            PrintTrackInfo("latitude_track",       classic, p->latitude_track.track_type,       p->latitude_track.sequence_keys.offset, p->latitude_track.sequence_keys.count);
            PrintTrackInfo("longitude_track",      classic, p->longitude_track.track_type,      p->longitude_track.sequence_keys.offset, p->longitude_track.sequence_keys.count);
            PrintTrackInfo("gravity_track",        classic, p->gravity_track.track_type,        p->gravity_track.sequence_keys.offset,  p->gravity_track.sequence_keys.count);
            PrintTrackInfo("life_track",           classic, p->life_track.track_type,           p->life_track.sequence_keys.offset,  p->life_track.sequence_keys.count);
            PrintTrackInfo("emission_rate_track",  classic, p->emission_rate_track.track_type,  p->emission_rate_track.sequence_keys.offset, p->emission_rate_track.sequence_keys.count);
            PrintTrackInfo("width_track",          classic, p->width_track.track_type,          p->width_track.sequence_keys.offset,  p->width_track.sequence_keys.count);
            PrintTrackInfo("length_track",         classic, p->length_track.track_type,         p->length_track.sequence_keys.offset, p->length_track.sequence_keys.count);
            PrintTrackInfo("zsource_track",        classic, p->zsource_track.track_type,        p->zsource_track.sequence_keys.offset, p->zsource_track.sequence_keys.count);
            PrintTrackInfo("visibility_track",     classic, p->visibility_track.track_type,     p->visibility_track.sequence_keys.offset, p->visibility_track.sequence_keys.count);
        }
    }
}

static void PrintRibbonEmitters(uint8_t const *data, uint32_t size, m2HeaderInfo_t const *header) {
    bool classic = header->version <= 263;
    uint32_t stride = classic ? sizeof(m2RibbonClassic_t) : sizeof(m2RibbonModern_t);
    uint32_t count = (uint32_t)header->ribbon_emitters.count;
    uint32_t off = (uint32_t)header->ribbon_emitters.offset;
    if (!count) return;
    if (off + count * stride > size) {
        printf("ribbon_emitters: %u entries but data out of bounds (off=%u stride=%u size=%u)\n",
               (unsigned)count, (unsigned)off, (unsigned)stride, (unsigned)size);
        return;
    }
    printf("ribbon_emitters: %u (%s)\n", (unsigned)count, classic ? "classic/tbc" : "modern");
    FOR_LOOP(i, count) {
        uint8_t const *raw = data + off + i * stride;
        if (classic) {
            m2RibbonClassic_t const *r = (m2RibbonClassic_t const *)raw;
            printf("  [%u] id=0x%x bone=%u tex_n=%u mat_n=%u edges_per_sec=%.2f life=%.2f grav=%.2f rows=%u cols=%u\n",
                   (unsigned)i, (unsigned)r->ribbon_id, (unsigned)r->bone_index,
                   (unsigned)r->texture_indices.count, (unsigned)r->material_indices.count,
                   r->edges_per_second, r->edge_lifetime, r->gravity,
                   (unsigned)r->texture_rows, (unsigned)r->texture_cols);
            PrintTrackInfo("color_track",        classic, r->color_track.track_type,        r->color_track.keys.offset,        r->color_track.keys.count);
            PrintTrackInfo("alpha_track",        classic, r->alpha_track.track_type,        r->alpha_track.keys.offset,        r->alpha_track.keys.count);
            PrintTrackInfo("height_above_track", classic, r->height_above_track.track_type, r->height_above_track.keys.offset, r->height_above_track.keys.count);
            PrintTrackInfo("height_below_track", classic, r->height_below_track.track_type, r->height_below_track.keys.offset, r->height_below_track.keys.count);
            PrintTrackInfo("texture_slot_track", classic, r->texture_slot_track.track_type, r->texture_slot_track.keys.offset, r->texture_slot_track.keys.count);
            PrintTrackInfo("visibility_track",   classic, r->visibility_track.track_type,   r->visibility_track.keys.offset,   r->visibility_track.keys.count);
        } else {
            m2RibbonModern_t const *r = (m2RibbonModern_t const *)raw;
            printf("  [%u] id=0x%x bone=%u tex_n=%u mat_n=%u edges_per_sec=%.2f life=%.2f grav=%.2f rows=%u cols=%u\n",
                   (unsigned)i, (unsigned)r->ribbon_id, (unsigned)r->bone_index,
                   (unsigned)r->texture_indices.count, (unsigned)r->material_indices.count,
                   r->edges_per_second, r->edge_lifetime, r->gravity,
                   (unsigned)r->texture_rows, (unsigned)r->texture_cols);
            PrintTrackInfo("color_track",        classic, r->color_track.track_type,        r->color_track.sequence_keys.offset,        r->color_track.sequence_keys.count);
            PrintTrackInfo("alpha_track",        classic, r->alpha_track.track_type,        r->alpha_track.sequence_keys.offset,        r->alpha_track.sequence_keys.count);
            PrintTrackInfo("height_above_track", classic, r->height_above_track.track_type, r->height_above_track.sequence_keys.offset, r->height_above_track.sequence_keys.count);
            PrintTrackInfo("height_below_track", classic, r->height_below_track.track_type, r->height_below_track.sequence_keys.offset, r->height_below_track.sequence_keys.count);
            PrintTrackInfo("texture_slot_track", classic, r->texture_slot_track.track_type, r->texture_slot_track.sequence_keys.offset, r->texture_slot_track.sequence_keys.count);
            PrintTrackInfo("visibility_track",   classic, r->visibility_track.track_type,   r->visibility_track.sequence_keys.offset,   r->visibility_track.sequence_keys.count);
        }
    }
}

static void PrintTextures(uint8_t const *data, uint32_t size, m2HeaderInfo_t const *header) {
    m2Texture_t const *textures = ArrayPtr(data, size, header->textures, sizeof(*textures));

    if (!textures) {
        printf("textures: unavailable\n");
        return;
    }
    printf("textures:\n");
    FOR_LOOP(i, (uint32_t)header->textures.count) {
        m2Texture_t const *texture = textures + i;
        cstring_t path = StringPtr(data, size, texture->filename);
        printf("  [%03u] type=%u (%s) flags=0x%08x path=%s\n",
               (unsigned)i,
               (unsigned)texture->type,
               TextureTypeName(texture->type),
               (unsigned)texture->flags,
               path && *path ? path : "<replaceable>");
    }
}

static void PrintTextureLookup(uint8_t const *data, uint32_t size, m2HeaderInfo_t const *header) {
    int16_t const *texture_lookup;

    if (!header || !g_dump_all || header->texture_lookup_table.count <= 0) {
        return;
    }

    texture_lookup = ArrayPtr(data, size, header->texture_lookup_table, sizeof(*texture_lookup));
    if (!texture_lookup) {
        printf("texture_lookup_table: unavailable\n");
        return;
    }

    printf("texture_lookup_table values:");
    FOR_LOOP(i, (uint32_t)header->texture_lookup_table.count) {
        printf(" %d", (int)texture_lookup[i]);
    }
    printf("\n");
}

static void PrintBatches(uint8_t const *m2_data,
                         uint32_t m2_size,
                         m2HeaderInfo_t const *header,
                         uint8_t const *skin_data,
                         uint32_t skin_size,
                         m2Array_t vertex_lookup_array,
                         m2Array_t indices_array,
                         m2Array_t sections_array,
                         m2Array_t batches_array,
                         bool legacy_sections,
                         cstring_t label) {
    m2Batch_t const *batches;
    m2Vertex_t const *vertices;
    uint16_t const *vertex_lookup, *indices;
    int16_t const *texture_lookup;

    if (!g_dump_all || !header || batches_array.count <= 0) {
        return;
    }

    batches = ArrayPtr(skin_data, skin_size, batches_array, sizeof(*batches));
    if (!batches) {
        printf("%s.batches: unavailable\n", label);
        return;
    }

    texture_lookup = ArrayPtr(m2_data, m2_size, header->texture_lookup_table, sizeof(*texture_lookup));
    vertices = ArrayPtr(m2_data, m2_size, header->vertices, sizeof(*vertices));
    vertex_lookup = ArrayPtr(skin_data, skin_size, vertex_lookup_array, sizeof(*vertex_lookup));
    indices = ArrayPtr(skin_data, skin_size, indices_array, sizeof(*indices));
    printf("%s.batches detail:\n", label);
    FOR_LOOP(i, (uint32_t)batches_array.count) {
        m2Batch_t const *batch = batches + i;
        int16_t texture_index = -1;
        uint16_t skin_section_id = 0xffff;
        uint32_t index_start = 0, index_count = 0;
        box3_t bounds = { 0 };
        bool has_bounds = false;
        if (texture_lookup && batch->texture_combo_index < (uint16_t)header->texture_lookup_table.count) {
            texture_index = texture_lookup[batch->texture_combo_index];
        }
        if (batch->skin_section_index < (uint16_t)sections_array.count) {
            if (legacy_sections) {
                m2SkinSectionLegacy_t const *sections = ArrayPtr(skin_data,
                                                                 skin_size,
                                                                 sections_array,
                                                                 sizeof(*sections));
                if (sections) {
                    skin_section_id = sections[batch->skin_section_index].skin_section_id;
                    index_start = sections[batch->skin_section_index].index_start;
                    index_count = sections[batch->skin_section_index].index_count;
                }
            } else {
                m2SkinSection_t const *sections = ArrayPtr(skin_data,
                                                          skin_size,
                                                          sections_array,
                                                          sizeof(*sections));
                if (sections) {
                    skin_section_id = sections[batch->skin_section_index].skin_section_id;
                    index_start = sections[batch->skin_section_index].index_start;
                    index_count = sections[batch->skin_section_index].index_count;
                }
            }
        }
        if (vertices && vertex_lookup && indices)
            FOR_LOOP(j, index_count) {
                uint32_t skin_index = index_start + j;
                if (skin_index >= (uint32_t)indices_array.count || indices[skin_index] >= (uint16_t)vertex_lookup_array.count)
                    continue;
                if (vertex_lookup[indices[skin_index]] >= (uint16_t)header->vertices.count) continue;
                UpdateBounds(&bounds, vertices[vertex_lookup[indices[skin_index]]].pos, &has_bounds);
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
        if (has_bounds)
            printf("        bounds min=%.4f %.4f %.4f max=%.4f %.4f %.4f\n",
                   bounds.min.x, bounds.min.y, bounds.min.z, bounds.max.x, bounds.max.y, bounds.max.z);
    }
}

static bool DefaultSkinPath(cstring_t model_path, string_t out, uint32_t out_size) {
    return CopyWithExtension(model_path, "00.skin", out, out_size);
}

static void PrintEmbeddedSkinInfo(uint8_t const *data, uint32_t size, m2HeaderInfo_t const *header) {
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
    FOR_LOOP(i, (uint32_t)header->views.count) {
        m2EmbeddedView_t const *view = views + i;
        printf("  view[%u]\n", (unsigned)i);
        PrintArray("view.vertices", view->vertices, false);
        PrintArray("view.indices", view->indices, false);
        PrintArray("view.bones", view->bones, false);
        PrintArray("view.sections", view->sections, false);
        PrintArray("view.batches", view->batches, false);
        printf("  %-28s %u\n", "view.bone_count_max", (unsigned)view->bone_count_max);
        PrintBatches(data, size, header, data, size, view->vertices, view->indices,
                     view->sections, view->batches, true, "view");
    }
}

static void PrintSkinInfo(cstring_t model_path, uint8_t const *m2_data, uint32_t m2_size, m2HeaderInfo_t const *header) {
    PATHSTR requested;
    PATHSTR resolved = { 0 };
    uint32_t size = 0;
    uint8_t * data;
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
        PrintBatches(m2_data, m2_size, header, data, size, skin->vertices, skin->indices,
                     skin->sections, skin->batches, false, "skin");
    } else {
        printf("skin: %s has unexpected magic %.4s\n", resolved, (char const *)&skin->magic);
    }
    Tool_MemFree(data);
}

static void AddSectionId(uint16_t *sections, uint32_t *count, uint32_t max_count, uint16_t section_id) {
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

static uint32_t CollectSkinSections(cstring_t model_path,
                                 uint8_t const *m2_data,
                                 uint32_t m2_size,
                                 m2HeaderInfo_t const *header,
                                 uint16_t *sections,
                                 uint32_t max_sections) {
    PATHSTR requested;
    PATHSTR resolved = { 0 };
    uint32_t size = 0;
    uint8_t * data;
    m2SkinHeader_t const *skin;
    uint32_t count = 0;

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
                FOR_LOOP(i, (uint32_t)skin->sections.count) {
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
                FOR_LOOP(i, (uint32_t)view->sections.count) {
                    AddSectionId(sections, &count, max_sections, skin_sections[i].skin_section_id);
                }
            }
        }
    }
    return count;
}

static void SortSections(uint16_t *sections, uint32_t count) {
    for (uint32_t i = 1; i < count; i++) {
        uint16_t value = sections[i];
        uint32_t j = i;
        while (j > 0 && sections[j - 1] > value) {
            sections[j] = sections[j - 1];
            j--;
        }
        sections[j] = value;
    }
}

static void PrintWowPlayerConfig(uint8_t const *m2_data, uint32_t m2_size, m2HeaderInfo_t const *header) {
    static cstring_t const slot_names[M2TOOL_CHAR_TEX_COUNT] = {
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
    uint16_t sections[256];
    uint32_t section_count;

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
        FOR_LOOP(priority, 7) {
            PATHSTR path;
            cstring_t stem = outfit.texture[i][priority];
            if (!ComponentTexturePath(stem, (uint8_t)i, g_model_path, path, sizeof(path))) continue;
            printf("    %-11s priority=%u stem=%s path=%s %s\n", slot_names[i], (unsigned)priority, stem, path,
                   Tool_FileExists(archives, sizeof(archives) / sizeof(archives[0]), path) ? "exists" : "missing");
        }
    }

    section_count = CollectSkinSections(g_model_path, m2_data, m2_size, header, sections, sizeof(sections) / sizeof(sections[0]));
    SortSections(sections, section_count);
    printf("  visible_section_ids:");
    FOR_LOOP(i, section_count) {
        if (WowVisibleSection(sections[i], &outfit, sections, section_count)) {
            printf(" %u", (unsigned)sections[i]);
        }
    }
    printf("\n");
    printf("  hidden_section_ids:");
    FOR_LOOP(i, section_count) {
        if (!WowVisibleSection(sections[i], &outfit, sections, section_count)) {
            printf(" %u", (unsigned)sections[i]);
        }
    }
    printf("\n");
    printf("  note: component textures are composed into the body skin; they should not directly select clothing overlay sections.\n");
}

static void PrintHeaderArrays(m2HeaderInfo_t const *h) {
    bool legacy = h->version <= 263;

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
    uint32_t file_size = 0;
    uint8_t * file_data;
    uint8_t const *payload;
    uint32_t payload_size;
    m2HeaderInfo_t header;
    cstring_t name;
    box3_t vertex_bounds;
    bool has_vertex_bounds;
    float header_ground_offset;
    float vertex_ground_offset = 0.0f;

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
        PrintEvents(payload, payload_size, &header);
        PrintParticleEmitters(payload, payload_size, &header);
        PrintRibbonEmitters(payload, payload_size, &header);
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

static bool LoadPreviewBounds(box3_t *bounds, float *extent_out) {
    PATHSTR resolved = { 0 };
    uint32_t file_size = 0;
    uint8_t * file_data;
    uint8_t const *payload;
    uint32_t payload_size;
    m2HeaderInfo_t header;
    float width;
    float depth;
    float height;

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

static void RenderViewerFrame(refExport_t const *re, model_t * model, uint32_t now, box3_t const * bounds) {
    viewDef_t viewdef = { 0 };
    renderEntity_t entity = { 0 };
    size2_t window = re->GetWindowSize();
    float aspect = window.height ? (float)window.width / (float)window.height : 1.0f;
    float height = bounds ? fabsf(bounds->max.z - bounds->min.z) : 2.5f;
    float radius = MAX(1.0f, height * g_preview_scale * 0.6f);
    float near_clip = MAX(0.01f, g_orbit.distance * 0.01f);
    float far_clip = MAX(100.0f, g_orbit.distance + radius * 8.0f);
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
    Viewer_OrbitBuildLight(&g_orbit, &(vector3_t){ 0.0f, 0.0f, 0.0f }, MAX(32.0f, radius * 2.0f), &viewdef.lightMatrix);
    viewdef.viewport = (rect_t){ 0, 0, 1, 1 };
    viewdef.scissor = (rect_t){ 0, 0, 1, 1 };
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
    model_t * model;
    box3_t bounds = { 0 };
    bool has_bounds;
    float extent = 2.5f;
    float height;
    float orbit_distance;
    bool running = true;

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
                     (vector3_t){ 0.0f, 0.0f, height * g_preview_scale * 0.55f },
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
            g_wow_appearance = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--equipment") && i + 1 < argc) {
            g_wow_player_config = true;
            g_wow_equipment = (uint32_t)strtoul(argv[++i], NULL, 0);
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
