#ifndef WOW_R_M2_FORMAT_H
#define WOW_R_M2_FORMAT_H

#include "common/shared.h"

typedef enum { M2_FORMAT_CLASSIC, M2_FORMAT_MODERN } m2Format_t;
typedef struct { int32_t size, offset; } m2Array_t;
typedef struct { VECTOR3 min, max; } m2Box_t;
typedef struct { uint16_t track_type, loop_index; m2Array_t sequence_times, sequence_keys; } m2Track_t;
typedef struct { uint16_t track_type, loop_index; m2Array_t ranges, times, keys; } m2TrackClassic_t;
typedef struct { m2Array_t times, values; } m2PartTrack_t;
typedef struct { m2Array_t times; } m2SequenceTimes_t;
typedef struct { m2Array_t keys; } m2SequenceKeys_t;
typedef struct { DWORD auCompQ[2]; } m2CompQuat_t;
typedef struct { DWORD start, end; } m2Range_t;
typedef struct { BYTE v[4]; } m2Ubyte4_t;
typedef SHORT m2Fixed16_t;

typedef struct {
    WORD animation_id, variation_index;
    DWORD start_timestamp, end_timestamp;
    FLOAT movespeed;
    DWORD flags;
    SHORT frequency;
    WORD padding;
    DWORD replay_min, replay_max, blend_time;
    m2Box_t bounds;
    FLOAT bounds_radius;
    SHORT next_animation;
    WORD alias_next;
} m2SequenceClassic_t;

typedef struct {
    WORD animation_id, variation_index;
    DWORD duration;
    FLOAT movespeed;
    DWORD flags, frequency, replay_min, replay_max, blend_time;
    m2Box_t bounds;
    FLOAT bounds_radius;
    SHORT next_animation;
    WORD alias_next;
} m2SequenceModern_t;

typedef struct {
    DWORD attachment_id;
    WORD bone_index, padding;
    VECTOR3 position;
    m2Track_t visibility_track;
} m2AttachmentModern_t;

typedef struct {
    DWORD attachment_id;
    WORD bone_index, padding;
    VECTOR3 position;
    m2TrackClassic_t visibility_track;
} m2AttachmentClassic_t;

typedef struct {
    DWORD camera_id;
    FLOAT fov, far_clip, near_clip;
    m2Track_t position_track;
    VECTOR3 position_pivot;
    m2Track_t target_track;
    VECTOR3 target_pivot;
    m2Track_t roll_track;
} m2CameraModern_t;

typedef struct {
    DWORD camera_id;
    FLOAT fov, far_clip, near_clip;
    m2TrackClassic_t position_track;
    VECTOR3 position_pivot;
    m2TrackClassic_t target_track;
    VECTOR3 target_pivot;
    m2TrackClassic_t roll_track;
} m2CameraClassic_t;

typedef struct {
    DWORD bone_id, flags;
    WORD parent_index, dist_to_parent;
    DWORD union_data;
    m2Track_t translation_track, rotation_track, scale_track;
    VECTOR3 pivot;
} m2CompBoneModern_t;

typedef struct {
    DWORD bone_id, flags;
    WORD parent_index, submesh_id;
    m2TrackClassic_t translation_track, rotation_track, scale_track;
    VECTOR3 pivot;
} m2CompBoneClassic_t;

typedef struct {
    DWORD magic, version;
    m2Array_t name;
    DWORD flags;
    m2Array_t global_loops, sequences, sequence_lookups, bones, key_bone_lookup, vertices;
    DWORD num_skin_profiles;
    m2Array_t colors, textures, texture_weights, texture_transforms, replaceable_texture_lookup, materials;
    m2Array_t bone_lookup_table, texture_lookup_table, tex_unit_lookup_table;
    m2Array_t transparency_lookup_table, texture_transforms_lookup_table;
    m2Box_t bounding_box;
    FLOAT bounding_sphere_radius;
    m2Box_t collision_box;
    FLOAT collision_sphere_radius;
    m2Array_t collision_indices, collision_positions, collision_normals, attachments, attachment_lookup;
    m2Array_t events, lights, cameras, camera_lookup, ribbons, particles, texture_combiner_combos;
} m2Header_t;

typedef struct {
    DWORD magic, version;
    m2Array_t name;
    DWORD flags;
    m2Array_t global_loops, sequences, sequence_lookups, playable_animation_lookup;
    m2Array_t bones, key_bone_lookup, vertices, views, colors, textures, transparency_lookup;
    m2Array_t texture_flipbooks, texture_animations, color_replacements, render_flags;
    m2Array_t bone_lookup_table, texture_lookup_table, tex_unit_lookup_table;
    m2Array_t transparency_lookup_table, texture_transforms_lookup_table;
    m2Box_t bounding_box;
    FLOAT bounding_sphere_radius;
    m2Box_t collision_box;
    FLOAT collision_sphere_radius;
    m2Array_t collision_indices, collision_positions, collision_normals, attachments, attachment_lookup;
    m2Array_t events, lights, cameras, camera_lookup, ribbons, particles;
} m2HeaderLegacy_t;

/* A resident M2 remains one file-shaped allocation; the version selects the active header view. */
typedef union {
    m2Header_t modern;
    m2HeaderLegacy_t classic;
} m2File_t;

typedef struct {
    VECTOR3 pos;
    BYTE bone_weights[4], bone_indices[4];
    VECTOR3 normal;
    VECTOR2 tex_coords[2];
} m2VertexDisk_t;

typedef struct { DWORD type, flags; m2Array_t filename; } m2TextureDisk_t;

typedef struct {
    DWORD magic;
    m2Array_t vertices, indices, bones, sections, batches;
    DWORD bone_count_max;
} m2SkinHeader_t;

typedef struct { m2Array_t vertices, indices, bones, sections, batches; DWORD bone_count_max; } m2LegacyView_t;

typedef struct {
    WORD skin_section_id, level, vertex_start, vertex_count, index_start, index_count;
    WORD bone_count, bone_combo_index, bone_influences, center_bone_index;
    VECTOR3 center_position, sort_center_position;
    FLOAT sort_radius;
} m2SkinSection_t;

typedef struct {
    WORD skin_section_id, level, vertex_start, vertex_count, index_start, index_count;
    WORD bone_count, bone_combo_index, bone_influences, center_bone_index;
    VECTOR3 center_position;
} m2SkinSectionLegacy_t;

typedef struct {
    BYTE flags;
    signed char priority_plane;
    WORD shader_id, skin_section_index, geoset_index;
    SHORT color_index;
    WORD material_index, material_layer, texture_count, texture_combo_index, texture_coord_combo_index;
    WORD texture_weight_combo_index, texture_transform_combo_index;
} m2Batch_t;

typedef struct {
    DWORD particle_id, flags;
    VECTOR3 position;
    WORD bone_index, texture_index;
    m2Array_t geometry_mdl, recursion_mdl;
    BYTE blend_mode, emitter_type;
    WORD color_index, pad;
    SHORT priority_plane;
    WORD rows, cols;
    m2Track_t speed_track, variation_track, latitude_track, longitude_track, gravity_track, life_track;
    FLOAT life_variation;
    m2Track_t emission_rate_track;
    FLOAT emission_rate_variation;
    m2Track_t width_track, length_track, zsource_track;
    m2PartTrack_t color_track, alpha_track, scale_track;
    VECTOR2 scale_variation;
    m2PartTrack_t head_cell_track, tail_cell_track;
    FLOAT tail_length, twinkle_fps, twinkle_onoff, twinkle_scale[2];
    FLOAT ivel_scale, drag, initial_spin, initial_spin_variation, spin, spin_variation;
    m2Box_t tumble;
    VECTOR3 wind_vector;
    FLOAT wind_time, follow_speed1, follow_scale1, follow_speed2, follow_scale2;
    m2Array_t spline;
    m2Track_t visibility_track;
} m2Particle_t;

typedef struct {
    DWORD particle_id, flags;
    VECTOR3 position;
    WORD bone_index, texture_index;
    m2Array_t geometry_mdl, recursion_mdl;
    BYTE blend_mode, emitter_type;
    WORD color_index, pad;
    SHORT priority_plane;
    WORD rows, cols;
    m2TrackClassic_t speed_track, variation_track, latitude_track, longitude_track, gravity_track, life_track;
    m2TrackClassic_t emission_rate_track, width_track, length_track, visibility_track;
    FLOAT midpoint;
    DWORD colors[3];
    FLOAT scales[3];
    BYTE tail[0x1f8 - 0x168];
} m2ParticleClassic_t;

typedef struct {
    DWORD ribbon_id;
    WORD bone_index, pad0;
    VECTOR3 position;
    m2Array_t texture_indices, material_indices;
    m2Track_t color_track, alpha_track, height_above_track, height_below_track;
    FLOAT edges_per_second, edge_lifetime, gravity;
    WORD texture_rows, texture_cols;
    m2Track_t texture_slot_track, visibility_track;
} m2Ribbon_t;

typedef struct {
    DWORD ribbon_id;
    WORD bone_index, pad0;
    VECTOR3 position;
    m2Array_t texture_indices, material_indices;
    m2TrackClassic_t color_track, alpha_track, height_above_track, height_below_track;
    FLOAT edges_per_second, edge_lifetime, gravity;
    WORD texture_rows, texture_cols;
    m2TrackClassic_t texture_slot_track, visibility_track;
    SHORT priority_plane;
    WORD pad1;
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
