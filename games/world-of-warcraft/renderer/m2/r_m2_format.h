#ifndef WOW_R_M2_FORMAT_H
#define WOW_R_M2_FORMAT_H

#include "common/shared.h"

typedef enum { M2_FORMAT_CLASSIC, M2_FORMAT_MODERN } m2Format_t;
typedef struct { int32_t size, offset; } m2Array_t;
typedef struct { vec3_t min, max; } m2Box_t;
typedef struct { uint16_t track_type, loop_index; m2Array_t sequence_times, sequence_keys; } m2Track_t;
typedef struct { uint16_t track_type, loop_index; m2Array_t ranges, times, keys; } m2TrackClassic_t;
typedef struct { m2Array_t times, values; } m2PartTrack_t;
typedef struct { m2Array_t times; } m2SequenceTimes_t;
typedef struct { m2Array_t keys; } m2SequenceKeys_t;
typedef struct { uint32_t auCompQ[2]; } m2CompQuat_t;
typedef struct { uint32_t start, end; } m2Range_t;
typedef struct { uint8_t v[4]; } m2Ubyte4_t;
typedef int16_t m2Fixed16_t;

typedef struct {
    uint16_t animation_id, variation_index;
    uint32_t start_timestamp, end_timestamp;
    float movespeed;
    uint32_t flags;
    int16_t frequency;
    uint16_t padding;
    uint32_t replay_min, replay_max, blend_time;
    m2Box_t bounds;
    float bounds_radius;
    int16_t next_animation;
    uint16_t alias_next;
} m2SequenceClassic_t;

typedef struct {
    uint16_t animation_id, variation_index;
    uint32_t duration;
    float movespeed;
    uint32_t flags, frequency, replay_min, replay_max, blend_time;
    m2Box_t bounds;
    float bounds_radius;
    int16_t next_animation;
    uint16_t alias_next;
} m2SequenceModern_t;

typedef struct {
    uint32_t attachment_id;
    uint16_t bone_index, padding;
    vec3_t position;
    m2Track_t visibility_track;
} m2AttachmentModern_t;

typedef struct {
    uint32_t attachment_id;
    uint16_t bone_index, padding;
    vec3_t position;
    m2TrackClassic_t visibility_track;
} m2AttachmentClassic_t;

/* Canonical M2 attachment type IDs (wowdev.wiki M2#Attachments; the client's CModelAttachmentId).
 * These are semantic type IDs resolved through the attachment_lookup table, never positional
 * array indices, so they are stable regardless of a model's attachments[] order. */
typedef enum {
    M2_ATTACH_SHIELD = 0,              /* also MountMain / ItemVisual0 */
    M2_ATTACH_HAND_RIGHT = 1,          /* ItemVisual1 */
    M2_ATTACH_HAND_LEFT = 2,           /* ItemVisual2 */
    M2_ATTACH_ELBOW_RIGHT = 3,         /* ItemVisual3 */
    M2_ATTACH_ELBOW_LEFT = 4,          /* ItemVisual4 */
    M2_ATTACH_SHOULDER_RIGHT = 5,
    M2_ATTACH_SHOULDER_LEFT = 6,
    M2_ATTACH_KNEE_RIGHT = 7,
    M2_ATTACH_KNEE_LEFT = 8,
    M2_ATTACH_HIP_RIGHT = 9,
    M2_ATTACH_HIP_LEFT = 10,
    M2_ATTACH_HELM = 11,
    M2_ATTACH_BACK = 12,
    M2_ATTACH_SHOULDER_FLAP_RIGHT = 13,
    M2_ATTACH_SHOULDER_FLAP_LEFT = 14,
    M2_ATTACH_CHEST_BLOOD_FRONT = 15,
    M2_ATTACH_CHEST_BLOOD_BACK = 16,
    M2_ATTACH_BREATH = 17,
    M2_ATTACH_PLAYER_NAME = 18,        /* CGUnit_C::GetNamePosition name-plate anchor */
    M2_ATTACH_BASE = 19,
    M2_ATTACH_HEAD = 20,
    M2_ATTACH_SPELL_LEFT_HAND = 21,
    M2_ATTACH_SPELL_RIGHT_HAND = 22,
    M2_ATTACH_SPECIAL1 = 23,
    M2_ATTACH_SPECIAL2 = 24,
    M2_ATTACH_SPECIAL3 = 25,
    M2_ATTACH_SHEATH_MAIN_HAND = 26,
    M2_ATTACH_SHEATH_OFF_HAND = 27,
    M2_ATTACH_SHEATH_SHIELD = 28,
    M2_ATTACH_PLAYER_NAME_MOUNTED = 29, /* GetNamePosition tries this before M2_ATTACH_PLAYER_NAME */
    M2_ATTACH_LARGE_WEAPON_LEFT = 30,
    M2_ATTACH_LARGE_WEAPON_RIGHT = 31,
    M2_ATTACH_HIP_WEAPON_LEFT = 32,
    M2_ATTACH_HIP_WEAPON_RIGHT = 33,
    M2_ATTACH_CHEST = 34,
    M2_ATTACH_HAND_ARROW = 35,
    M2_ATTACH_BULLET = 36,
    M2_ATTACH_SPELL_HAND_OMNI = 37,
    M2_ATTACH_SPELL_HAND_DIRECTED = 38,
    M2_ATTACH_VEHICLE_SEAT1 = 39,      /* Wrath+ */
    M2_ATTACH_VEHICLE_SEAT2 = 40,
    M2_ATTACH_VEHICLE_SEAT3 = 41,
    M2_ATTACH_VEHICLE_SEAT4 = 42,
    M2_ATTACH_VEHICLE_SEAT5 = 43,
    M2_ATTACH_VEHICLE_SEAT6 = 44,
    M2_ATTACH_VEHICLE_SEAT7 = 45,
    M2_ATTACH_VEHICLE_SEAT8 = 46,
    M2_ATTACH_LEFT_FOOT = 47,
    M2_ATTACH_RIGHT_FOOT = 48,
    M2_ATTACH_SHIELD_NO_GLOVE = 49,
    M2_ATTACH_SPINE_LOW = 50,
    M2_ATTACH_ALTERED_SHOULDER_R = 51,
    M2_ATTACH_ALTERED_SHOULDER_L = 52,
    M2_ATTACH_BELT_BUCKLE = 53,        /* MoP+ */
    M2_ATTACH_SHEATH_CROSSBOW = 54,
    M2_ATTACH_HEAD_TOP = 55,           /* Legion+ */
    M2_ATTACH_VIRTUAL_SPELL_DIRECTED = 56, /* BfA+ */
    M2_ATTACH_BACKPACK = 57,           /* BfA+ */
    M2_ATTACH_UNKNOWN_BFA = 60,        /* BfA+; undocumented */
} m2AttachmentId_t;

typedef struct {
    uint32_t camera_id;
    float fov, far_clip, near_clip;
    m2Track_t position_track;
    vec3_t position_pivot;
    m2Track_t target_track;
    vec3_t target_pivot;
    m2Track_t roll_track;
} m2CameraModern_t;

typedef struct {
    uint32_t camera_id;
    float fov, far_clip, near_clip;
    m2TrackClassic_t position_track;
    vec3_t position_pivot;
    m2TrackClassic_t target_track;
    vec3_t target_pivot;
    m2TrackClassic_t roll_track;
} m2CameraClassic_t;

typedef struct {
    uint32_t bone_id, flags;
    uint16_t parent_index, dist_to_parent;
    uint32_t union_data;
    m2Track_t translation_track, rotation_track, scale_track;
    vec3_t pivot;
} m2CompBoneModern_t;

typedef struct {
    uint32_t bone_id, flags;
    uint16_t parent_index, submesh_id;
    m2TrackClassic_t translation_track, rotation_track, scale_track;
    vec3_t pivot;
} m2CompBoneClassic_t;

typedef struct {
    uint32_t magic, version;
    m2Array_t name;
    uint32_t flags;
    m2Array_t global_loops, sequences, sequence_lookups, bones, key_bone_lookup, vertices;
    uint32_t num_skin_profiles;
    m2Array_t colors, textures, texture_weights, texture_transforms, replaceable_texture_lookup, materials;
    m2Array_t bone_lookup_table, texture_lookup_table, tex_unit_lookup_table;
    m2Array_t transparency_lookup_table, texture_transforms_lookup_table;
    m2Box_t bounding_box;
    float bounding_sphere_radius;
    m2Box_t collision_box;
    float collision_sphere_radius;
    m2Array_t collision_indices, collision_positions, collision_normals, attachments, attachment_lookup;
    m2Array_t events, lights, cameras, camera_lookup, ribbons, particles, texture_combiner_combos;
} m2Header_t;

typedef struct {
    uint32_t magic, version;
    m2Array_t name;
    uint32_t flags;
    m2Array_t global_loops, sequences, sequence_lookups, playable_animation_lookup;
    m2Array_t bones, key_bone_lookup, vertices, views, colors, textures, transparency_lookup;
    m2Array_t texture_flipbooks, texture_animations, color_replacements, render_flags;
    m2Array_t bone_lookup_table, texture_lookup_table, tex_unit_lookup_table;
    m2Array_t transparency_lookup_table, texture_transforms_lookup_table;
    m2Box_t bounding_box;
    float bounding_sphere_radius;
    m2Box_t collision_box;
    float collision_sphere_radius;
    m2Array_t collision_indices, collision_positions, collision_normals, attachments, attachment_lookup;
    m2Array_t events, lights, cameras, camera_lookup, ribbons, particles;
} m2HeaderLegacy_t;

/* A resident M2 remains one file-shaped allocation; the version selects the active header view. */
typedef union {
    m2Header_t modern;
    m2HeaderLegacy_t classic;
} m2File_t;

typedef struct {
    vec3_t pos;
    uint8_t bone_weights[4], bone_indices[4];
    vec3_t normal;
    vec2_t tex_coords[2];
} m2VertexDisk_t;

typedef struct { uint32_t type, flags; m2Array_t filename; } m2TextureDisk_t;

typedef struct {
    uint32_t magic;
    m2Array_t vertices, indices, bones, sections, batches;
    uint32_t bone_count_max;
} m2SkinHeader_t;

typedef struct { m2Array_t vertices, indices, bones, sections, batches; uint32_t bone_count_max; } m2LegacyView_t;

typedef struct {
    uint16_t skin_section_id, level, vertex_start, vertex_count, index_start, index_count;
    uint16_t bone_count, bone_combo_index, bone_influences, center_bone_index;
    vec3_t center_position, sort_center_position;
    float sort_radius;
} m2SkinSection_t;

typedef struct {
    uint16_t skin_section_id, level, vertex_start, vertex_count, index_start, index_count;
    uint16_t bone_count, bone_combo_index, bone_influences, center_bone_index;
    vec3_t center_position;
} m2SkinSectionLegacy_t;

typedef struct {
    uint8_t flags;
    signed char priority_plane;
    uint16_t shader_id, skin_section_index, geoset_index;
    int16_t color_index;
    uint16_t material_index, material_layer, texture_count, texture_combo_index, texture_coord_combo_index;
    uint16_t texture_weight_combo_index, texture_transform_combo_index;
} m2Batch_t;

typedef struct {
    uint32_t particle_id, flags;
    vec3_t position;
    uint16_t bone_index, texture_index;
    m2Array_t geometry_mdl, recursion_mdl;
    uint8_t blend_mode, emitter_type;
    uint16_t color_index, pad;
    int16_t priority_plane;
    uint16_t rows, cols;
    m2Track_t speed_track, variation_track, latitude_track, longitude_track, gravity_track, life_track;
    float life_variation;
    m2Track_t emission_rate_track;
    float emission_rate_variation;
    m2Track_t width_track, length_track, zsource_track;
    m2PartTrack_t color_track, alpha_track, scale_track;
    vec2_t scale_variation;
    m2PartTrack_t head_cell_track, tail_cell_track;
    float tail_length, twinkle_fps, twinkle_onoff, twinkle_scale[2];
    float ivel_scale, drag, initial_spin, initial_spin_variation, spin, spin_variation;
    m2Box_t tumble;
    vec3_t wind_vector;
    float wind_time, follow_speed1, follow_scale1, follow_speed2, follow_scale2;
    m2Array_t spline;
    m2Track_t visibility_track;
} m2Particle_t;

typedef struct {
    uint32_t particle_id, flags;
    vec3_t position;
    uint16_t bone_index, texture_index;
    m2Array_t geometry_mdl, recursion_mdl;
    uint8_t blend_mode, emitter_type;
    uint16_t color_index, pad;
    int16_t priority_plane;
    uint16_t rows, cols;
    m2TrackClassic_t speed_track, variation_track, latitude_track, longitude_track, gravity_track, life_track;
    m2TrackClassic_t emission_rate_track, width_track, length_track, visibility_track;
    float midpoint;
    uint32_t colors[3];
    float scales[3];
    uint8_t tail[0x1f8 - 0x168];
} m2ParticleClassic_t;

typedef struct {
    uint32_t ribbon_id;
    uint16_t bone_index, pad0;
    vec3_t position;
    m2Array_t texture_indices, material_indices;
    m2Track_t color_track, alpha_track, height_above_track, height_below_track;
    float edges_per_second, edge_lifetime, gravity;
    uint16_t texture_rows, texture_cols;
    m2Track_t texture_slot_track, visibility_track;
} m2Ribbon_t;

typedef struct {
    uint32_t ribbon_id;
    uint16_t bone_index, pad0;
    vec3_t position;
    m2Array_t texture_indices, material_indices;
    m2TrackClassic_t color_track, alpha_track, height_above_track, height_below_track;
    float edges_per_second, edge_lifetime, gravity;
    uint16_t texture_rows, texture_cols;
    m2TrackClassic_t texture_slot_track, visibility_track;
    int16_t priority_plane;
    uint16_t pad1;
} m2RibbonClassic_t;

_Static_assert(sizeof(m2Track_t) == 20, "modern M2 tracks are 20 bytes");
_Static_assert(sizeof(m2TrackClassic_t) == 28, "classic M2 tracks are 28 bytes");
_Static_assert(sizeof(m2SequenceClassic_t) == 68, "classic M2 sequences are 68 bytes");
_Static_assert(sizeof(m2SequenceModern_t) == 64, "modern M2 sequences are 64 bytes");
_Static_assert(sizeof(m2CompBoneClassic_t) == 108, "classic M2 bones are 108 bytes");
_Static_assert(sizeof(m2CompBoneModern_t) == 88, "modern M2 bones are 88 bytes");
_Static_assert(sizeof(m2Particle_t) == 0x1dc, "modern M2 particles are 0x1dc bytes");
_Static_assert(sizeof(m2ParticleClassic_t) == 0x1f8, "classic M2 particles are 0x1f8 bytes");
_Static_assert(sizeof(m2Ribbon_t) == 0xac, "modern M2 ribbons are 0xac bytes");
_Static_assert(sizeof(m2RibbonClassic_t) == 0xe0, "classic M2 ribbons are 0xe0 bytes");

#endif
